#include "core/Engine.hpp"
#include "core/Random.hpp"

#include <algorithm>
#include <cmath>

namespace talea {

namespace {

// Tolerance for float step times landing on a loop boundary (e.g. triplets).
constexpr double kEps = 1e-9;
/** How far before its boundary a scheduled mute or pattern switch lands. */
constexpr double kMuteLead = 1e-6;

const Pattern& defaultPattern() {
	static const Pattern p;
	return p;
}

/** Time until a step comes round again: one pass of the steps, or Length if shorter. */
double cycleQuarters(const Track& t) {
	double cycle = t.steps() * t.stepQuarters();
	double length = t.lengthQuarters();
	return length > 0 ? std::min(cycle, length) : cycle;
}

/** Step length of cycle `c` in quarter notes, without building the cycle's Track. */
double stepOf(const Track& base, const TrackCycles& cyc, int c) {
	int i = int(cyc.value(base, c, TP_DIVISION));
	return kDivisions[std::clamp(i, 0, countOf(kDivisions) - 1)].quarters;
}

double lengthOf(const Track& base, const TrackCycles& cyc, int c) {
	int i = int(cyc.value(base, c, TP_LENGTH));
	return kLengths[std::clamp(i, 0, countOf(kLengths) - 1)].quarters;
}

/** Largest Timing + Delay shift of cycle `c` (see Track::maxStepShift), assuming the
worst for randomised, step-locked or modulated ones. */
double maxShiftOf(const Track& base, const TrackCycles& cyc, const TrackRandom& rnd, const StepLocks& locks,
                  const float* mod, int c) {
	int sub = kSwingSubdivisions[std::clamp(base.swingSubdivision, 0, countOf(kSwingSubdivisions) - 1)];
	uint32_t locked = locks.used | cyc.cycles[c].locks.used;
	auto varies = [&](TrackParam p) { return rnd.amount[p] != 0.f || (locked & (1u << p)) || mod[p] != 0.f; };
	double delay = varies(TP_DELAY) ? 1.0 : std::fabs(cyc.value(base, c, TP_DELAY)) / 100.0;
	double timing = varies(TP_TIMING) ? 1.0 : std::fabs(cyc.value(base, c, TP_TIMING)) / 100.0;
	return (delay + timing * 0.5 * sub) * stepOf(base, cyc, c);
}

/** CV offsets onto `t`, like random's: Rotate wraps, Repeats stays finite unless the knob is
at infinite. */
void applyMod(Track& t, const float* mod) {
	for (int i = 0; i < kNumTrackParams; i++) {
		TrackParam p = TrackParam(i);
		if (mod[p] == 0.f || !randomisable(p))
			continue;
		float v = t.values[p] + mod[p];
		const ParamInfo& info = kTrackParams[p];
		if (p == TP_ROTATE) {
			int n = t.steps();
			int r = int(std::lround(v)) % n;
			t.values[p] = float(r < 0 ? r + n : r);
		}
		else if (p == TP_REPEATS && t.values[p] < info.max)
			t.values[p] = std::min(clampParam(p, v), info.max - 1);
		else
			t.set(p, v);
	}
}

long floorDiv(long a, long b) {
	long q = a / b;
	return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

struct Pulse {
	double time;
	long loop, step; // grid position: cycle span index, step within it
	int cycle;       // cycle slot whose settings play it
	double grid;     // grid time before Timing / Delay
	long randomLoop; // loop count the per-cycle random values follow
	int lockPos;     // step whose locks it plays with, -1 = none
};
// Pulses landing in a one-step slice: at most ~2 x reach / step + 2, and reach <= 3 steps.
constexpr int kMaxSlicePulses = 16;

} // namespace

Engine::Engine() : slots_(kNumPatterns) {
	for (int& c : soloCycle)
		c = -1;
	defaultPattern(); // built here rather than on the audio thread
}

void EventList::sort() {
	// Insertion sort: lists are short and mostly ordered already.
	for (int i = 1; i < size; i++) {
		NoteEvent e = events[i];
		int j = i - 1;
		while (j >= 0 && (events[j].time > e.time || (events[j].time == e.time && events[j].on && !e.on))) {
			events[j + 1] = events[j];
			j--;
		}
		events[j + 1] = e;
	}
}

StepMask Engine::hits(int track) {
	return hitsOf(track, 0, pattern.tracks[track]);
}

StepMask Engine::hits(int track, int cycle) {
	const TrackCycles& cyc = pattern.cycles[track];
	if (cycle < 0 || cycle >= kMaxCycles || !cyc.cycles[cycle].modified())
		return hits(track);
	return hitsOf(track, cycle, cyc.resolve(pattern.tracks[track], cycle));
}

StepMask Engine::hitsOf(int track, int slot, const Track& t) {
	HitCache& c = cache_[track][slot];
	int steps = t.steps(), pulses = t.pulses(), rot = t.getInt(TP_ROTATE);
	if (steps != c.steps || pulses != c.pulses || rot != c.rotate || t.manualHits != c.manual
		|| t.removedHits != c.removed) {
		c.steps = steps;
		c.pulses = pulses;
		c.rotate = rot;
		c.manual = t.manualHits;
		c.removed = t.removedHits;
		c.hits = t.hits();
	}
	return c.hits;
}

int Engine::cycleList(int track, int* list) const {
	int solo = soloCycle[track];
	if (solo >= 0 && solo < kMaxCycles) {
		list[0] = solo;
		return 1;
	}
	int n = std::clamp(pattern.cycles[track].count, 1, kMaxCycles);
	for (int i = 0; i < n; i++)
		list[i] = i;
	return n;
}

Engine::Span Engine::spanByIndex(int track, long index) const {
	const Track& base = pattern.tracks[track];
	const TrackCycles& cyc = pattern.cycles[track];
	int list[kMaxCycles];
	int n = cycleList(track, list);
	if (n == 1) {
		// One cycle loops on its Length; with an infinite Length it is a single span.
		double length = lengthOf(base, cyc, list[0]);
		if (length <= 0)
			return {0, list[0], 0.0, INFINITY};
		return {index, list[0], index * length, (index + 1) * length};
	}
	double d[kMaxCycles], period = 0.0;
	for (int j = 0; j < n; j++) {
		d[j] = cyc.loopQuarters(base, list[j]);
		period += d[j];
	}
	long rep = floorDiv(index, n);
	int j = int(index - rep * n);
	double start = rep * period;
	for (int i = 0; i < j; i++)
		start += d[i];
	return {index, list[j], start, start + d[j]};
}

Engine::Span Engine::spanAt(int track, double t) const {
	const Track& base = pattern.tracks[track];
	const TrackCycles& cyc = pattern.cycles[track];
	t = std::max(t, 0.0);
	int list[kMaxCycles];
	int n = cycleList(track, list);
	if (n == 1) {
		double length = lengthOf(base, cyc, list[0]);
		return spanByIndex(track, length > 0 ? long(std::floor(t / length)) : 0);
	}
	double d[kMaxCycles], period = 0.0;
	for (int j = 0; j < n; j++) {
		d[j] = cyc.loopQuarters(base, list[j]);
		period += d[j];
	}
	long rep = long(std::floor(t / period));
	double start = rep * period;
	for (int j = 0; j < n; j++) {
		if (t < start + d[j] || j == n - 1)
			return {rep * n + j, list[j], start, start + d[j]};
		start += d[j];
	}
	return {0, list[0], 0.0, d[0]};
}

int Engine::cycleAt(int track, double t) const {
	return spanAt(track, t - origin_).cycle;
}

void Engine::process(double from, double to, EventList& out) {
	if (!(to > from))
		return;
	if (!(std::fabs(from - lastTo_) < kEps))
		clearBursts();
	lastTo_ = to;
	// Scheduled mutes and pattern switches land just before their boundary so a pulse on it
	// follows them. At the same boundary the mutes go first, in the pattern they were for.
	for (;;) {
		double muteAt = scheduled_ && scheduledTime_ - kMuteLead <= to ? std::max(from, scheduledTime_ - kMuteLead) : INFINITY;
		double switchAt = queued_ >= 0 && switchTime_ - kMuteLead <= to ? std::max(from, switchTime_ - kMuteLead) : INFINITY;
		double songTime = nextSongEvent(from);
		double songAt = songTime - kMuteLead <= to ? std::max(from, songTime - kMuteLead) : INFINITY;
		double split = std::min(std::min(std::min(muteAt, switchAt), songAt), to);
		processTracks(from, split, out);
		// At one boundary: mutes, then a switch of the player's (it overrides the song), then
		// the song's.
		if (muteAt <= switchAt && muteAt <= songAt && muteAt < INFINITY)
			applyDueMutes(split, out);
		else if (switchAt <= songAt && switchAt < INFINITY) {
			int slot = queued_, chainPos = queuedChainPos_;
			queued_ = -1;
			queuedChainPos_ = -1;
			songOverride(switchTime_);
			switchTo(slot, switchTime_, split, out);
			if (chainPos >= 0)
				chainPos_ = chainPos;
			scheduleChainNext();
		}
		else if (songAt < INFINITY) {
			if (!songEvent(songTime, split, out)) {
				// The song ended: nothing plays past it.
				lastTo_ = split;
				break;
			}
		}
		else
			break;
		from = split;
	}
	out.sort();
}

void Engine::processTracks(double from, double to, EventList& out) {
	if (!(to > from))
		return;
	// Tracks run on pattern time; their events go out on host time.
	int first = out.size;
	for (int i = 0; i < kNumTracks; i++)
		processTrack(i, from - origin_, to - origin_, out);
	for (int i = first; i < out.size; i++)
		out.events[i].time += origin_;
}

void Engine::setMuted(int track, bool muted, double time, EventList& out) {
	Track& t = pattern.tracks[track];
	if (t.muted == muted)
		return;
	t.muted = muted;
	if (!muted || !muteCuts)
		return;
	Voice& v = voices_[track];
	if (v.active) {
		out.push({std::min(v.offTime + origin_, time), track, false, v.note, 0});
		v.active = false;
	}
	for (Burst& b : bursts_[track])
		b.live = false;
}

void Engine::applyDueMutes(double time, EventList& out) {
	if (!scheduled_ || scheduledTime_ - kMuteLead > time)
		return;
	for (int i = 0; i < kNumTracks; i++) {
		if (scheduled_ & (1u << i))
			setMuted(i, !pattern.tracks[i].muted, time, out);
	}
	scheduled_ = 0;
}

double Engine::nextBoundary(int timing, double now) const {
	double unit = timing == MUTE_BEAT ? 1.0 : timing == MUTE_BAR ? 4.0 : 0.0;
	if (timing == MUTE_PATTERN)
		unit = patternQuarters() > 0 ? patternQuarters() : 4.0;
	// Strictly after `now`: events at `now` have already played.
	return unit > 0 ? origin_ + (std::floor((now - origin_) / unit + kEps) + 1.0) * unit : now;
}

void Engine::commitMutes(double now) {
	if (!staged_)
		return;
	scheduledTime_ = nextBoundary(muteTiming, now);
	scheduled_ ^= staged_;
	staged_ = 0;
}

bool Engine::patternUsed(int slot) const {
	if (slot == current_)
		return committedPattern() != defaultPattern();
	return used_ & (1u << slot);
}

void Engine::storePattern(int slot, const Pattern& p) {
	if (slot == current_) {
		pattern = p;
		tempActive_ = false;
		return;
	}
	slots_[slot] = p;
	if (p != defaultPattern())
		used_ |= uint16_t(1u << slot);
	else
		used_ &= uint16_t(~(1u << slot));
}

void Engine::clearPattern(int slot) {
	storePattern(slot, defaultPattern());
}

void Engine::makeCurrent(int slot) {
	if (slot == current_)
		return;
	endTemp(false);
	int leaving = current_;
	current_ = -1; // so storePattern() files the leaving pattern in its slot
	storePattern(leaving, pattern);
	current_ = slot;
	pattern = slots_[slot];
	used_ &= uint16_t(~(1u << slot));
}

void Engine::setCurrentPattern(int slot) {
	makeCurrent(slot);
}

void Engine::resetBank() {
	tempActive_ = false;
	for (Pattern& p : slots_)
		p.reset();
	used_ = 0;
	pattern.reset();
	current_ = 0;
	queued_ = -1;
	clearChain();
	clearMuteQueue();
}

void Engine::switchTo(int slot, double origin, double cut, EventList& out) {
	allNotesOff(cut, out);
	clearMuteQueue();
	makeCurrent(slot);
	origin_ = origin;
	switches_++;
}

void Engine::queue(int slot, int timing, int chainPos, double now, EventList& out) {
	if (timing == MUTE_NOW) {
		queued_ = -1;
		queuedChainPos_ = -1;
		// Just after `now`, so the new pattern's first step falls in the next window.
		double at = now < 0.0 ? 0.0 : now + kMuteLead;
		songOverride(at);
		switchTo(slot, at, now, out);
		if (chainPos >= 0)
			chainPos_ = chainPos;
		scheduleChainNext();
		return;
	}
	queued_ = slot;
	queuedTiming_ = timing;
	queuedChainPos_ = chainPos;
	switchTime_ = nextBoundary(timing, now);
}

void Engine::scheduleChainNext() {
	if (chainPos_ < 0 || chainLen_ < 2)
		return;
	int next = (chainPos_ + 1) % chainLen_;
	queued_ = chain_[next];
	queuedTiming_ = MUTE_PATTERN;
	queuedChainPos_ = next;
	switchTime_ = nextBoundary(MUTE_PATTERN, origin_);
}

void Engine::queuePattern(int slot, double now, EventList& out, bool immediate) {
	clearChain();
	if (slot == current_ && !immediate && patternQuantize != MUTE_NOW) {
		queued_ = -1;
		return;
	}
	queue(slot, immediate ? int(MUTE_NOW) : patternQuantize, -1, now, out);
}

void Engine::chainPattern(int slot, double now, EventList& out, bool immediate) {
	if (chainLen_ >= kMaxChain)
		return;
	chain_[chainLen_++] = slot;
	if (chainLen_ == 1) {
		// The chain starts with the playing pattern, or once its first entry is in.
		queued_ = -1;
		queuedChainPos_ = -1;
		if (slot == current_)
			chainPos_ = 0;
		else
			queue(slot, immediate ? int(MUTE_NOW) : patternQuantize, 0, now, out);
		return;
	}
	// Nothing next yet, or the wrap to the first entry: the new entry comes next.
	if (chainPos_ >= 0 && (queued_ < 0 || (queuedChainPos_ == 0 && chainPos_ == chainLen_ - 2))) {
		int next = chainPos_ + 1;
		queued_ = chain_[next];
		queuedTiming_ = MUTE_PATTERN;
		queuedChainPos_ = next;
		switchTime_ = nextBoundary(MUTE_PATTERN, now);
	}
}

void Engine::clearChain() {
	chainLen_ = 0;
	chainPos_ = -1;
	if (queuedChainPos_ >= 0)
		queued_ = -1;
	queuedChainPos_ = -1;
}

void Engine::rewind() {
	origin_ = 0.0;
	if (queued_ >= 0)
		switchTime_ = nextBoundary(queuedTiming_, -kEps);
	clearBursts();
}

void Engine::endTemp(bool keep) {
	if (!tempActive_)
		return;
	tempActive_ = false;
	if (keep)
		return;
	bool muted[kNumTracks];
	for (int i = 0; i < kNumTracks; i++)
		muted[i] = pattern.tracks[i].muted;
	pattern = tempSaved_;
	for (int i = 0; i < kNumTracks; i++)
		pattern.tracks[i].muted = muted[i];
}

void Engine::processTrack(int track, double from, double to, EventList& out) {
	const Track& base = pattern.tracks[track];
	const TrackCycles& cyc = pattern.cycles[track];
	Voice& v = voices_[track];
	Burst* bursts = bursts_[track];

	// Each pulse and its repeats play with the settings of the cycle the pulse falls in.
	// Cycles without overrides use the track itself; the others are built when first needed.
	for (bool& valid : resolvedValid_)
		valid = false;
	auto trackFor = [&](int c) -> const Track& {
		if (!cyc.cycles[c].modified())
			return base;
		if (!resolvedValid_[c]) {
			resolved_[c] = cyc.resolve(base, c);
			resolvedValid_[c] = true;
		}
		return resolved_[c];
	};

	// Random offsets go on top of the cycle's settings, drawn for the note's loop and grid
	// time. One scratch copy: each caller is done with it before the next call.
	// Cycles with random amount overrides get their own random settings, built when first
	// needed like their Tracks.
	const TrackRandom& rnd = pattern.random[track];
	const bool trackRandomActive = rnd.active();
	for (bool& valid : randomValid_)
		valid = false;
	auto randomFor = [&](int c, bool& active) -> const TrackRandom& {
		if (!cyc.cycles[c].randomOverridden) {
			active = trackRandomActive;
			return rnd;
		}
		if (!randomValid_[c]) {
			randomResolved_[c] = cyc.resolveRandom(rnd, c);
			randomActive_[c] = randomResolved_[c].active();
			randomValid_[c] = true;
		}
		active = randomActive_[c];
		return randomResolved_[c];
	};
	// Step locks replace the cycle's values on their step (the cycle's own locks over the
	// track's); random goes on top of them.
	const StepLocks& locks = pattern.locks[track];
	auto lockedAt = [&](int c, int pos) { return locks.any(pos) || cyc.cycles[c].locks.any(pos); };
	Track randomised;
	// Host CV offsets (IN expander) go on last.
	const float* trackMod = mod[track];
	bool modOn = false;
	for (int p = 0; p < kNumTrackParams; p++)
		modOn |= trackMod[p] != 0.f && randomisable(TrackParam(p));
	auto settingsFor = [&](int c, long randomLoop, double grid, int lockPos) -> const Track& {
		const Track& t = trackFor(c);
		bool active;
		const TrackRandom& r = randomFor(c, active);
		bool locked = lockPos >= 0 && lockedAt(c, lockPos);
		if (!active && !locked && !modOn)
			return t;
		randomised = t;
		if (locked) {
			locks.apply(randomised, lockPos);
			cyc.cycles[c].locks.apply(randomised, lockPos);
		}
		if (active)
			r.apply(randomised, randomLoop, grid);
		if (modOn)
			applyMod(randomised, trackMod);
		return randomised;
	};

	// Probability follows a fixed random sequence keyed on the note's position, so the same
	// notes are skipped on every replay; the knob only sets how many.
	auto chance = [&](uint64_t pulseId, int repeat) {
		return Random::hashUniform(pulseId ^ (uint64_t(repeat) * 0x9E3779B97F4A7C15ull) ^ (uint64_t(track) << 56));
	};

	auto noteOff = [&](double time) {
		out.push({time, track, false, v.note, 0});
		v.active = false;
	};

	auto noteOn = [&](const Track& t, double time, const int* chord, int chordSize, int velocity, double gap) {
		double gate = t.gateQuarters(gap);
		if (v.active)
			noteOff(std::min(v.offTime, time));
		v.active = true;
		v.note = chord[0];
		v.onTime = time;
		v.offTime = time + gate;
		NoteEvent e{time, track, true, v.note, velocity};
		e.chordSize = chordSize;
		for (int i = 0; i < chordSize; i++)
			e.chord[i] = uint8_t(chord[i]);
		if (t.cc) {
			e.cc = true;
			e.ccNumber = t.ccNumber;
			e.glide = t.glideQuarters(gap);
		}
		out.push(e);
	};

	// Style motion: in Choke mode every note played moves it on (restarting each cycle
	// span); in Tail mode pulses play the starting position and each burst's repeats move.
	int chord[kMaxPool];
	auto chordFor = [&](const Track& t, long ordinal, double time) {
		return t.chordFor(ordinal, time, chord, phraseCv[track], transpose[track]);
	};

	// Plays due repeats, earliest first across bursts, up to `limit`.
	auto playRepeats = [&](double limit, bool inclusive) {
		for (;;) {
			Burst* next = nullptr;
			double nextTime = 0.0;
			for (int b = 0; b < kMaxBursts; b++) {
				Burst& burst = bursts[b];
				if (!burst.live)
					continue;
				const Track& t = settingsFor(burst.cycle, burst.randomLoop, burst.grid, burst.lockPos);
				int repeats = t.repeats();
				double time = std::max(burst.prev + t.repeatInterval(burst.fired), from);
				// An infinite burst lasts until its pulse comes round again.
				bool finished = repeats == kInfiniteRepeats ? time >= burst.start + cycleQuarters(t) - kEps
				                                            : burst.fired >= repeats;
				if (finished) {
					burst.live = false;
					continue;
				}
				if ((time < limit || (inclusive && time <= limit)) && (!next || time < nextTime)) {
					next = &burst;
					nextTime = time;
				}
			}
			if (!next)
				break;
			const Track& t = settingsFor(next->cycle, next->randomLoop, next->grid, next->lockPos);
			next->fired++;
			next->prev = nextTime;
			// A Tail repeat landing on a note that just started on this track adds nothing.
			if (std::fabs(nextTime - v.onTime) < kEps)
				continue;
			if (t.skipChance() > 0.0 && t.skipsRepeats() && chance(next->id, next->fired) < t.skipChance())
				continue;
			if (v.active && v.offTime <= nextTime)
				noteOff(v.offTime);
			int velocity = t.accented(t.repeatVelocity(next->fired), nextTime);
			long ordinal = t.repeatTail ? next->fired : v.ordinal++;
			int n = chordFor(t, ordinal, nextTime);
			noteOn(t, nextTime, chord, n, velocity, t.repeatInterval(next->fired));
		}
	};

	auto startBurst = [&](const Track& t, double time, uint64_t id, const Pulse& p) {
		if (!t.repeatTail) {
			for (int b = 0; b < kMaxBursts; b++)
				bursts[b].live = false;
		}
		if (t.repeats() == 0)
			return;
		Burst* slot = &bursts[0];
		for (int b = 0; b < kMaxBursts; b++) {
			if (!bursts[b].live) {
				slot = &bursts[b];
				break;
			}
			if (bursts[b].start < slot->start)
				slot = &bursts[b]; // all busy: replace the oldest
		}
		*slot = Burst{true, time, time, 0, id, p.cycle, p.grid, p.randomLoop, p.lockPos};
	};

	// A skipped or muted pulse plays nothing: no note, no repeats, and it doesn't choke
	// earlier repeats.
	auto playPulse = [&](const Pulse& p) {
		playRepeats(p.time, false);
		if (base.muted || cyc.cycles[p.cycle].muted)
			return; // earlier repeats finish unless the mute cut them
		const Track& t = settingsFor(p.cycle, p.randomLoop, p.grid, p.lockPos);
		// Probability phase moves the skip pattern along the steps.
		uint64_t id = (uint64_t(p.loop) << 32) ^ uint64_t(p.step - pattern.random[track].probPhase);
		if (t.skipChance() > 0.0 && chance(id, 0) < t.skipChance())
			return;
		if (v.active && v.offTime <= p.time)
			noteOff(v.offTime);
		// With repeats, the pulse's gate follows the repeat spacing like the repeats do.
		double gap = t.repeats() != 0 ? t.repeatInterval(0) : t.stepQuarters();
		if (p.loop != v.ordinalLoop) {
			v.ordinalLoop = p.loop;
			v.ordinal = 0;
		}
		long ordinal = t.repeatTail ? 0 : v.ordinal++;
		int n = chordFor(t, ordinal, p.time);
		noteOn(t, p.time, chord, n, t.accented(t.getInt(TP_VELOCITY), p.time), gap);
		startBurst(t, p.time, id, p);
	};

	// Calls fn(gridTime, span, k) for each grid step with gridTime in (lo, hi] and >= 0.
	// Each cycle span restarts the step count.
	auto forEachGridStep = [&](double lo, double hi, auto fn) {
		if (hi < 0.0)
			return;
		for (Span s = spanAt(track, lo);; s = spanByIndex(track, s.index + 1)) {
			if (s.start > hi)
				break;
			const double step = stepOf(base, cyc, s.cycle);
			for (long k = std::max(0L, long(std::floor((lo - s.start) / step)));; k++) {
				double time = s.start + k * step;
				if (time >= s.end - kEps || time > hi)
					break;
				if (time > lo)
					fn(time, s, k);
			}
			if (s.end == INFINITY)
				break;
		}
	};

	// Timing and Delay move pulses up to `reach` away from their grid step, so walk the
	// window in slices of the shortest step, gathering the pulses that land in each slice
	// from the grid steps near it, and play them in time order.
	int list[kMaxCycles];
	int n = cycleList(track, list);
	double slice = INFINITY, reach = 0.0;
	bool active;
	for (int i = 0; i < n; i++) {
		slice = std::min(slice, stepOf(base, cyc, list[i]));
		reach = std::max(reach, maxShiftOf(base, cyc, randomFor(list[i], active), locks, trackMod, list[i]));
	}
	reach += kEps;
	for (double sliceFrom = from; sliceFrom < to;) {
		double sliceTo = std::min(sliceFrom + slice, to);
		Pulse pulses[kMaxSlicePulses];
		int count = 0;
		forEachGridStep(sliceFrom - reach, sliceTo + reach, [&](double grid, const Span& s, long k) {
			// An endless span counts its loops per pass of the steps.
			long randomLoop = s.end == INFINITY ? k / trackFor(s.cycle).steps() : s.index;
			const Track& unlocked = settingsFor(s.cycle, randomLoop, grid, -1);
			StepMask mask = hitsOf(track, s.cycle, unlocked);
			int step = int(k % unlocked.steps());
			if (!hasHit(mask, step))
				return;
			int pos = unlocked.unrotated(step);
			int lockPos = lockedAt(s.cycle, pos) ? pos : -1;
			const Track& t = lockPos >= 0 ? settingsFor(s.cycle, randomLoop, grid, lockPos) : unlocked;
			double time = grid + t.stepShift(k);
			if (time > sliceFrom && time <= sliceTo && count < kMaxSlicePulses)
				pulses[count++] = {time, s.index, k, s.cycle, grid, randomLoop, lockPos};
		});
		std::sort(pulses, pulses + count, [](const Pulse& a, const Pulse& b) { return a.time < b.time; });
		for (int i = 0; i < count; i++)
			playPulse(pulses[i]);
		sliceFrom = sliceTo;
	}
	playRepeats(to, true);
	if (v.active && v.offTime <= to)
		noteOff(v.offTime);
}

void Engine::allNotesOff(double time, EventList& out) {
	for (int i = 0; i < kNumTracks; i++) {
		Voice& v = voices_[i];
		if (v.active) {
			out.push({time, i, false, v.note, 0});
			v.active = false;
		}
	}
	clearBursts();
}

void Engine::clearBursts() {
	for (auto& track : bursts_) {
		for (Burst& b : track)
			b.live = false;
	}
	for (Voice& v : voices_)
		v.ordinalLoop = -1;
}

void Engine::lockRandom(int track, double now) {
	TrackRandom& r = pattern.random[track];
	double t = std::max(now - origin_, 0.0);
	double rateQuarters = kRandomRates[std::clamp(r.rate, 0, countOf(kRandomRates) - 1)].quarters;
	r.lockTick = long(std::floor(t / rateQuarters + 1e-9));
	// The loop count notes use (see processTrack): an endless span counts passes of the steps.
	Span s = spanAt(track, t);
	if (s.end == INFINITY) {
		const Track& base = pattern.tracks[track];
		const TrackCycles& cyc = pattern.cycles[track];
		long k = long(std::floor((t - s.start) / stepOf(base, cyc, s.cycle) + kEps));
		r.lockLoop = k / std::max(cyc.resolve(base, s.cycle).steps(), 1);
	}
	else
		r.lockLoop = s.index;
	r.locked = true;
}

Track Engine::settingsAt(int track, double t) const {
	t = std::max(t - origin_, 0.0);
	const Track& base = pattern.tracks[track];
	const TrackCycles& cyc = pattern.cycles[track];
	Span s = spanAt(track, t);
	Track out = cyc.resolve(base, s.cycle);
	long k = long(std::floor((t - s.start) / out.stepQuarters() + kEps));
	double grid = s.start + k * out.stepQuarters();
	long randomLoop = s.end == INFINITY ? k / out.steps() : s.index;
	TrackRandom rnd = cyc.resolveRandom(pattern.random[track], s.cycle);
	// Rotate (and so the step's position) can be random: place the locks after it.
	Track unlocked = out;
	if (rnd.active())
		rnd.apply(unlocked, randomLoop, grid);
	int pos = unlocked.unrotated(int(k % unlocked.steps()));
	pattern.locks[track].apply(out, pos);
	cyc.cycles[s.cycle].locks.apply(out, pos);
	if (rnd.active())
		rnd.apply(out, randomLoop, grid);
	applyMod(out, mod[track]);
	return out;
}

int Engine::stepAt(int track, double t) const {
	t -= origin_;
	if (t < 0.0)
		return -1;
	const Track& base = pattern.tracks[track];
	const TrackCycles& cyc = pattern.cycles[track];
	Span s = spanAt(track, t);
	int steps = std::clamp(int(cyc.value(base, s.cycle, TP_STEPS)), 1, kMaxSteps);
	long k = long(std::floor((t - s.start) / stepOf(base, cyc, s.cycle) + kEps));
	return int(k % steps);
}

/** End-of-pattern period of `p`, with every cycle playing (no solo). */
double quartersOf(const Pattern& p) {
	double longest = 0.0;
	for (int i = 0; i < kNumTracks; i++) {
		const Track& base = p.tracks[i];
		const TrackCycles& cyc = p.cycles[i];
		int n = std::clamp(cyc.count, 1, kMaxCycles);
		double period = 0.0;
		for (int c = 0; c < n; c++)
			period += cyc.loopQuarters(base, c);
		longest = std::max(longest, period);
	}
	return longest;
}

// ---------------------------------------------------------------- song

int Song::totalBars() const {
	int n = 0;
	for (int i = 0; i < count; i++)
		n += entries[i].bars;
	return n;
}

bool Song::insert(int at, SongEntry e) {
	if (count >= kMaxSongEntries)
		return false;
	at = std::clamp(at, 0, count);
	for (int i = count; i > at; i--)
		entries[i] = entries[i - 1];
	e.slot = std::clamp(e.slot, 0, kNumPatterns - 1);
	e.bars = std::clamp(e.bars, 1, kMaxSongBars);
	entries[at] = e;
	count++;
	return true;
}

void Song::remove(int at) {
	if (at < 0 || at >= count)
		return;
	for (int i = at; i < count - 1; i++)
		entries[i] = entries[i + 1];
	count--;
	entries[count] = SongEntry();
}

bool Song::operator==(const Song& o) const {
	if (count != o.count || loop != o.loop)
		return false;
	for (int i = 0; i < count; i++) {
		if (!(entries[i] == o.entries[i]))
			return false;
	}
	return true;
}

int Engine::defaultBars(int slot) const {
	double q = quartersOf(patternSlot(std::clamp(slot, 0, kNumPatterns - 1)));
	return std::clamp(int(std::ceil(q / 4.0 - 1e-6)), 1, kMaxSongBars);
}

namespace {
/** First bar line strictly after `now` (host bars from time 0); time 0 itself at or
before it. */
double nextBar(double now) {
	return now <= 0.0 ? 0.0 : (std::floor(now / 4.0 + 1e-9) + 1.0) * 4.0;
}
} // namespace

void Engine::setSongOn(bool on, double now, EventList& out) {
	if (on == songOn_)
		return;
	songOn_ = on;
	songPos_ = -1;
	overridden_ = false;
	jumpPending_ = false;
	if (on)
		startSong(now, out);
}

void Engine::startSong(double now, EventList& out) {
	if (!songOn_)
		return;
	overridden_ = false;
	songPos_ = -1;
	jumpPending_ = false;
	if (song.count == 0)
		return;
	if (now <= 0.0) {
		startEntry(0, 0, 0.0, now, out);
		return;
	}
	jumpPending_ = true;
	jumpTime_ = nextBar(now);
	jumpEntry_ = 0;
	jumpBar_ = 0;
}

void Engine::resumeSong(double now) {
	if (!songOn_ || !overridden_)
		return;
	jumpPending_ = true;
	jumpTime_ = nextBar(now);
	jumpEntry_ = resumeEntry_;
	jumpBar_ = resumeBar_;
	// The song takes over again: the player's queue and chain go.
	clearChain();
	queued_ = -1;
}

void Engine::songOverride(double at) {
	if (!songOn_)
		return;
	if (jumpPending_) {
		// A start or resume was waiting: it is where the song picks up later.
		jumpPending_ = false;
		overridden_ = true;
		resumeEntry_ = jumpEntry_;
		resumeBar_ = jumpBar_;
		return;
	}
	if (overridden_ || songPos_ < 0 || songPos_ >= song.count)
		return;
	overridden_ = true;
	resumeEntry_ = songPos_;
	resumeBar_ = std::max(int(std::floor((at - entryStart_) / 4.0 + 1e-6)), 0);
}

double Engine::nextSongEvent(double from) const {
	if (!songOn_)
		return INFINITY;
	if (jumpPending_)
		return std::max(jumpTime_, from + kMuteLead);
	if (!songDriving())
		return INFINITY;
	double end = entryStart_ + 4.0 * song.entries[songPos_].bars;
	double next = std::max(end, from + kMuteLead);
	// The pattern restarts each time it ends within the entry.
	double pq = quartersOf(pattern);
	if (pq > kEps) {
		double restart = origin_ + (std::floor((from + kMuteLead - origin_) / pq + kEps) + 1.0) * pq;
		if (restart < end - kEps)
			next = std::min(next, restart);
	}
	return next;
}

void Engine::startEntry(int e, int bar, double at, double cut, EventList& out) {
	// Past an entry's end (it was shortened): the next one from its start.
	while (e < song.count && bar >= song.entries[e].bars) {
		e++;
		bar = 0;
	}
	if (e >= song.count) {
		songPos_ = -1;
		return;
	}
	clearChain();
	queued_ = -1;
	switchTo(song.entries[e].slot, at, cut, out);
	songPos_ = e;
	overridden_ = false;
	jumpPending_ = false;
	entryStart_ = at - 4.0 * bar;
	// Back mid-entry: the pattern where it would be, counting its restarts from bar 1.
	double pq = quartersOf(pattern);
	if (bar > 0 && pq > kEps)
		origin_ = at - std::fmod(at - entryStart_, pq);
}

bool Engine::songEvent(double at, double cut, EventList& out) {
	if (jumpPending_) {
		int e = jumpEntry_, bar = jumpBar_;
		jumpPending_ = false;
		while (e < song.count && bar >= song.entries[e].bars) {
			e++;
			bar = 0;
		}
		if (e >= song.count) {
			if (!song.loop || song.count == 0) {
				songPos_ = -1;
				overridden_ = false;
				songEnded_ = true;
				return false;
			}
			e = 0;
			bar = 0;
		}
		startEntry(e, bar, at, cut, out);
		return true;
	}
	double end = entryStart_ + 4.0 * song.entries[songPos_].bars;
	if (at < end - kEps) {
		// The pattern ended within the entry: it restarts from step 1.
		origin_ = at;
		for (Voice& v : voices_)
			v.ordinalLoop = -1;
		restarts_++;
		return true;
	}
	int next = songPos_ + 1;
	if (next >= song.count) {
		if (!song.loop) {
			songPos_ = -1;
			songEnded_ = true;
			return false;
		}
		next = 0;
	}
	startEntry(next, 0, at, cut, out);
	return true;
}

void Engine::songInsert(int at, SongEntry e, double now) {
	at = std::clamp(at, 0, song.count);
	if (!song.insert(at, e))
		return;
	bool started = songPos_ >= 0 || overridden_;
	if (songPos_ >= at)
		songPos_++;
	if (overridden_ && resumeEntry_ >= at)
		resumeEntry_++;
	if (jumpPending_ && started && jumpEntry_ >= at)
		jumpEntry_++;
	if (songOn_ && !started && !jumpPending_) {
		// The first entry of an empty song: it starts on the next bar line.
		jumpPending_ = true;
		jumpTime_ = nextBar(now);
		jumpEntry_ = 0;
		jumpBar_ = 0;
	}
}

void Engine::songRemove(int at, double now) {
	if (at < 0 || at >= song.count)
		return;
	bool playingIt = songDriving() && songPos_ == at;
	song.remove(at);
	if (songPos_ > at)
		songPos_--;
	if (overridden_) {
		if (resumeEntry_ > at)
			resumeEntry_--;
		else if (resumeEntry_ == at)
			resumeBar_ = 0;
	}
	if (jumpPending_) {
		if (jumpEntry_ > at)
			jumpEntry_--;
		else if (jumpEntry_ == at)
			jumpBar_ = 0;
	}
	if (playingIt) {
		// The playing entry went: the one after it starts on the next bar line.
		jumpPending_ = true;
		jumpTime_ = nextBar(now);
		jumpEntry_ = at;
		jumpBar_ = 0;
	}
}

void Engine::songClear() {
	song = Song();
	songPos_ = -1;
	overridden_ = false;
	jumpPending_ = false;
}

double Engine::patternQuarters() const {
	double longest = 0.0;
	for (int i = 0; i < kNumTracks; i++) {
		const Track& base = pattern.tracks[i];
		const TrackCycles& cyc = pattern.cycles[i];
		int list[kMaxCycles];
		int n = cycleList(i, list);
		double period = 0.0;
		for (int j = 0; j < n; j++)
			period += cyc.loopQuarters(base, list[j]);
		longest = std::max(longest, period);
	}
	return longest;
}

} // namespace talea
