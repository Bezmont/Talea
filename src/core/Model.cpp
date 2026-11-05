#include "core/Model.hpp"
#include "core/Random.hpp"

#include <algorithm>
#include <cmath>

namespace talea {

void Track::reset() {
	for (int i = 0; i < kNumTrackParams; i++)
		values[i] = kTrackParams[i].def;
	manualHits = 0;
	removedHits = 0;
	repeatTail = false;
	groove = GROOVE_AGOGO;
	grooveLength = kDefaultGrooveLength;
	swingSubdivision = 0;
	pool[0] = kBaseNote;
	poolSize = 1;
	scale = 0;
	root = 0;
	userScale = kDefaultUserScale;
	style = STYLE_FIXED;
	mono = false;
	voicingDown = false;
	phrase = PHRASE_CADENCE_1;
	phraseLength = kDefaultGrooveLength;
	muted = false;
	cc = false;
	ccNumber = kDefaultCc;
}

float clampParam(TrackParam p, float v) {
	const ParamInfo& info = kTrackParams[p];
	v = std::clamp(v, info.min, info.max);
	return info.snap ? std::round(v) : v;
}

void Track::set(TrackParam p, float v) {
	values[p] = clampParam(p, v);
}

int Track::steps() const {
	return std::clamp(getInt(TP_STEPS), 1, kMaxSteps);
}

int Track::pulses() const {
	return std::clamp(getInt(TP_PULSES), 0, steps());
}

StepMask Track::hits() const {
	int n = steps();
	StepMask generated = euclid(n, pulses()) & ~removedHits;
	return rotate(generated | (manualHits & fullMask(n)), n, getInt(TP_ROTATE));
}

int Track::unrotated(int step) const {
	int n = steps();
	return ((step - getInt(TP_ROTATE)) % n + n) % n;
}

void Track::toggleStep(int step) {
	int n = steps();
	if (step < 0 || step >= n)
		return;
	int pos = unrotated(step);
	StepMask bit = StepMask(1) << pos;
	bool generated = euclid(n, pulses()) & bit;
	bool on = (manualHits & bit) || (generated && !(removedHits & bit));
	if (on) {
		manualHits &= ~bit;
		if (generated)
			removedHits |= bit;
	}
	else if (removedHits & bit)
		removedHits &= ~bit;
	else
		manualHits |= bit;
}

int Track::repeats() const {
	int r = getInt(TP_REPEATS);
	return r > kMaxRepeats ? kInfiniteRepeats : std::max(r, 0);
}

double Track::repeatInterval(int i) const {
	double time = kRepeatTimes[std::clamp(getInt(TP_REPEAT_TIME), 0, countOf(kRepeatTimes) - 1)].quarters;
	// Full Pace doubles (CW) or halves (CCW) the spacing every 4 repeats, within 8x either way.
	double factor = std::exp2(std::clamp(get(TP_PACE) / 100.0 * i / 4.0, -3.0, 3.0));
	return time * factor;
}

int Track::repeatVelocity(int i) const {
	int count = repeats() == kInfiniteRepeats ? kInfiniteRampRepeats : repeats();
	double frac = count > 0 ? std::min(double(i) / count, 1.0) : 1.0;
	double ramp = get(TP_RAMP) / 100.0;
	// CW: repeats start quieter and build back up to Velocity on the last one.
	// CCW: repeats fade from Velocity down to (1 - |Ramp|) x Velocity on the last one.
	double scale = ramp >= 0 ? 1.0 - ramp * (1.0 - frac) : 1.0 + ramp * frac;
	return std::clamp(int(std::lround(get(TP_VELOCITY) * scale)), minVelocity(), 127);
}

double Track::gateQuarters(double interval) const {
	// Sustain 50 % = the whole gap; 100 % = twice the gap (overlapping).
	return interval * std::max(get(TP_SUSTAIN) / 50.0, 0.02);
}

double Track::glideQuarters(double interval) const {
	return interval * get(TP_SUSTAIN) / 100.0;
}

double Track::skipChance() const {
	return std::min(std::fabs(get(TP_PROBABILITY)) / 100.0, 1.0);
}

bool Track::skipsRepeats() const {
	return get(TP_PROBABILITY) > 0;
}

namespace {

// One bar of 1/16 accents per percussion groove, -1 (softest) .. 1 (loudest).
constexpr float kPercussionGrooves[4][16] = {
	{1, -1, .5f, -1, 1, -1, .5f, -.5f, 1, -1, .5f, -1, 1, -.5f, .5f, -1},      // Agogo
	{1, -.5f, -1, .5f, -1, 1, -.5f, -1, 1, -.5f, -1, .5f, -1, 1, .5f, -1},     // Timbales
	{.5f, -1, -.5f, 1, .5f, -1, 1, -.5f, .5f, -1, -.5f, 1, .5f, -1, 1, 1},     // Congas
	{1, 0, -1, .5f, -1, .5f, 1, -.5f, 1, 0, -1, .5f, -1, .5f, 1, 0},           // Bongo
};

constexpr double kPi = 3.14159265358979323846;

} // namespace

double Track::grooveValue(double time) const {
	if (groove < GROOVE_SAW) {
		double inBar = time / 4.0 - std::floor(time / 4.0);
		return kPercussionGrooves[groove][std::clamp(int(inBar * 16.0), 0, 15)];
	}
	double span = 4.0 * kGrooveBars[std::clamp(grooveLength, 0, countOf(kGrooveBars) - 1)];
	double phase = time / span - std::floor(time / span);
	switch (groove) {
		case GROOVE_SAW: return 2.0 * phase - 1.0;
		case GROOVE_TRIANGLE: return phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase;
		case GROOVE_SINE: return std::sin(2.0 * kPi * phase);
		default: return phase < 0.5 ? 1.0 : -1.0; // Pulse
	}
}

int Track::accented(int velocity, double time) const {
	double amount = get(TP_ACCENT) / 100.0;
	if (amount <= 0.0)
		return velocity;
	return std::clamp(int(std::lround(velocity + amount * kAccentDepth * grooveValue(time))), minVelocity(), 127);
}

double Track::stepShift(long k) const {
	double step = stepQuarters();
	double shift = get(TP_DELAY) / 100.0 * step;
	int sub = kSwingSubdivisions[std::clamp(swingSubdivision, 0, countOf(kSwingSubdivisions) - 1)];
	// Every 2nd unit of `sub` steps moves; full Timing = half a unit.
	if (k % sub == 0 && (k / sub) % 2 == 1)
		shift += get(TP_TIMING) / 100.0 * 0.5 * sub * step;
	return shift;
}

double Track::maxStepShift() const {
	double step = stepQuarters();
	int sub = kSwingSubdivisions[std::clamp(swingSubdivision, 0, countOf(kSwingSubdivisions) - 1)];
	return (std::fabs(get(TP_DELAY)) / 100.0 + std::fabs(get(TP_TIMING)) / 100.0 * 0.5 * sub) * step;
}

double Track::stepQuarters() const {
	return kDivisions[std::clamp(getInt(TP_DIVISION), 0, countOf(kDivisions) - 1)].quarters;
}

double Track::lengthQuarters() const {
	return kLengths[std::clamp(getInt(TP_LENGTH), 0, countOf(kLengths) - 1)].quarters;
}

double Track::loopQuarters() const {
	double length = lengthQuarters();
	return length > 0 ? length : steps() * stepQuarters();
}

int Track::playedSteps() const {
	double length = lengthQuarters();
	if (length <= 0)
		return steps();
	// Steps starting before the loop ends (the tolerance matches the engine's).
	return std::min(steps(), int(std::ceil(length / stepQuarters() - 1e-9)));
}

ScaleMask Track::scaleMask() const {
	return scale == kUserScale ? userScale : kScales[std::clamp(scale, 0, kNumScales - 1)].mask;
}

bool Track::inPool(int note) const {
	return std::find(pool, pool + poolSize, note) != pool + poolSize;
}

bool Track::togglePoolNote(int note) {
	if (note < 0 || note > 127)
		return false;
	int* end = pool + poolSize;
	int* it = std::find(pool, end, note);
	if (it != end) {
		if (poolSize == 1)
			return false;
		std::copy(it + 1, end, it);
		poolSize--;
		return true;
	}
	if (poolSize == kMaxPool)
		return false;
	pool[poolSize++] = note;
	std::sort(pool, pool + poolSize);
	return true;
}

void Track::harmony(int dir) {
	int index = dir > 0 ? 0 : poolSize - 1;
	int note = pool[index];
	do
		note = scaleNeighbour(note, scaleMask(), root, dir);
	while (inPool(note));
	if (note < 0 || note > 127)
		return;
	pool[index] = note;
	std::sort(pool, pool + poolSize);
}

int Track::noteFor(long ordinal, double time, double phraseCv, int transpose) const {
	int chord[kMaxPool];
	chordFor(ordinal, time, chord, phraseCv, transpose);
	return chord[0];
}

int Track::chordFor(long ordinal, double time, int* out, double phraseCv, int transpose) const {
	int chord[kMaxPool];
	int count = std::clamp(poolSize, 1, kMaxPool);
	for (int i = 0; i < count; i++)
		chord[i] = pool[i] + getInt(TP_PITCH);
	int amount = getInt(TP_VOICING);
	int n;
	if (mono && count > 1) {
		// Arpeggio: one chord note per event; the voicing moves on after each full pass.
		voice(chord, count, stylePosition(style, ordinal / count, amount), !voicingDown);
		out[0] = chord[ordinal % count];
		n = 1;
	}
	else {
		voice(chord, count, stylePosition(style, ordinal, amount), !voicingDown);
		std::copy(chord, chord + count, out);
		n = count;
	}
	double span = 4.0 * kGrooveBars[std::clamp(phraseLength, 0, countOf(kGrooveBars) - 1)];
	int shift = int(std::lround(getInt(TP_RANGE) * phraseValue(phrase, time / span, phraseCv))) + transpose;
	for (int i = 0; i < n; i++)
		out[i] = std::clamp(quantize(out[i] + shift, scaleMask(), root), 0, 127);
	std::sort(out, out + n);
	return int(std::unique(out, out + n) - out);
}

void TrackCycles::reset() {
	count = 1;
	for (CycleOverrides& c : cycles)
		c = CycleOverrides();
}

float TrackCycles::value(const Track& base, int c, TrackParam p) const {
	return cycles[c].has(p) ? cycles[c].values[p] : base.values[p];
}

void TrackCycles::set(int c, TrackParam p, float v) {
	cycles[c].values[p] = clampParam(p, v);
	cycles[c].overridden |= 1u << p;
}

void TrackCycles::clear(int c, TrackParam p) {
	cycles[c].overridden &= ~(1u << p);
}

void TrackCycles::clearCycle(int c) {
	cycles[c] = CycleOverrides();
}

void TrackCycles::shift(TrackParam p, float delta) {
	for (CycleOverrides& c : cycles) {
		if (c.has(p))
			c.values[p] = clampParam(p, c.values[p] + delta);
	}
}

namespace {

void copyTonal(Track& to, const Track& from) {
	to.scale = from.scale;
	to.root = from.root;
	to.userScale = from.userScale;
	std::copy(from.pool, from.pool + kMaxPool, to.pool);
	to.poolSize = from.poolSize;
}

} // namespace

Track TrackCycles::resolve(const Track& base, int c) const {
	Track t = base;
	for (int p = 0; p < kNumTrackParams; p++) {
		if (cycles[c].has(TrackParam(p)))
			t.values[p] = cycles[c].values[p];
	}
	if (cycles[c].ownHits) {
		t.manualHits = cycles[c].manualHits;
		t.removedHits = cycles[c].removedHits;
	}
	if (cycles[c].ownTonal)
		copyTonal(t, cycles[c].tonal);
	return t;
}

Track& TrackCycles::tonalEdit(const Track& base, int c) {
	CycleOverrides& o = cycles[c];
	if (!o.ownTonal) {
		copyTonal(o.tonal, base);
		o.ownTonal = true;
	}
	return o.tonal;
}

void TrackCycles::setTonal(int c, const Track& t) {
	copyTonal(cycles[c].tonal, t);
	cycles[c].ownTonal = true;
}

float TrackCycles::randomAmount(const TrackRandom& rnd, int c, TrackParam p) const {
	return cycles[c].hasRandom(p) ? cycles[c].randomAmounts[p] : rnd.amount[p];
}

void TrackCycles::setRandom(int c, TrackParam p, float amount) {
	if (!randomisable(p))
		return;
	cycles[c].randomAmounts[p] = std::clamp(amount, -100.f, 100.f);
	cycles[c].randomOverridden |= 1u << p;
}

void TrackCycles::clearRandom(int c, TrackParam p) {
	cycles[c].randomOverridden &= ~(1u << p);
}

TrackRandom TrackCycles::resolveRandom(const TrackRandom& rnd, int c) const {
	TrackRandom r = rnd;
	for (int p = 0; p < kNumTrackParams; p++) {
		if (cycles[c].hasRandom(TrackParam(p)))
			r.amount[p] = cycles[c].randomAmounts[p];
	}
	return r;
}

void TrackCycles::toggleStep(const Track& base, int c, int step) {
	Track t = resolve(base, c);
	t.toggleStep(step);
	CycleOverrides& o = cycles[c];
	o.ownHits = true;
	o.manualHits = t.manualHits;
	o.removedHits = t.removedHits;
}

double TrackCycles::loopQuarters(const Track& base, int c) const {
	// Called for every cycle on each processed window: read the three values directly
	// rather than building the cycle's Track.
	double length = kLengths[std::clamp(int(value(base, c, TP_LENGTH)), 0, countOf(kLengths) - 1)].quarters;
	if (length > 0)
		return length;
	int steps = std::clamp(int(value(base, c, TP_STEPS)), 1, kMaxSteps);
	return steps * kDivisions[std::clamp(int(value(base, c, TP_DIVISION)), 0, countOf(kDivisions) - 1)].quarters;
}

bool randomisable(TrackParam p) {
	return p != TP_STEPS && p != TP_DIVISION && p != TP_LENGTH;
}

void TrackRandom::reset(int track) {
	*this = TrackRandom();
	seed = 0x5EED0000u + uint32_t(track);
}

bool TrackRandom::active() const {
	for (int p = 0; p < kNumTrackParams; p++) {
		if (amount[p] != 0.f && randomisable(TrackParam(p)))
			return true;
	}
	return false;
}

namespace {

/** How far back draw() looks for the pass that last re-rolled a value. */
constexpr long kMaxVarianceScan = 256;

uint64_t randomKey(uint32_t seed, int p, int slot, long pass, uint64_t salt) {
	return uint64_t(seed) * 0x9E3779B97F4A7C15ull ^ uint64_t(p) * 0xC2B2AE3D27D4EB4Full
	       ^ uint64_t(slot) * 0x165667B19E3779F9ull ^ uint64_t(pass) * 0xD6E8FEB86659FD93ull ^ salt;
}

} // namespace

double TrackRandom::draw(TrackParam p, long u) const {
	long pass = u >= 0 ? u / kRandomSequence : (u + 1) / kRandomSequence - 1;
	int slot = int(u - pass * kRandomSequence);
	// The value comes from the latest pass that re-rolled this slot (pass 0 always does).
	long gen = 0;
	if (variance >= 100.f)
		gen = pass;
	else if (variance > 0.f) {
		double chance = variance / 100.0;
		for (gen = pass; gen > 0 && pass - gen < kMaxVarianceScan; gen--) {
			if (Random::hashUniform(randomKey(seed, p, slot, gen, 0x5A17)) < chance)
				break;
		}
	}
	return Random::hashUniform(randomKey(seed, p, slot, gen, 0));
}

long TrackRandom::position(TrackParam p, long u) const {
	auto floorDiv = [](long a, long b) { return a >= 0 ? a / b : (a + 1) / b - 1; };
	u += phase;
	if (locked) {
		long pass = floorDiv((isPerCycle(p) ? lockLoop : lockTick) + phase, kRandomSequence);
		u = pass * kRandomSequence + (u - floorDiv(u, kRandomSequence) * kRandomSequence);
	}
	return u;
}

void TrackRandom::regenerate() {
	seed = uint32_t(Random::hashUniform(uint64_t(seed) * 0x9E3779B97F4A7C15ull + 1) * 4294967296.0);
}

void TrackRandom::apply(Track& t, long loop, double time) const {
	double rateQuarters = kRandomRates[std::clamp(rate, 0, countOf(kRandomRates) - 1)].quarters;
	long tick = long(std::floor(time / rateQuarters + 1e-9));
	// Slew: over the last `slew` of a tick the value glides to the next tick's.
	double glide = 0.0;
	if (slew > 0.f) {
		double s = std::min(slew, 100.f) / 100.0;
		glide = std::clamp((time / rateQuarters - tick - (1.0 - s)) / s, 0.0, 1.0);
	}
	auto value = [&](TrackParam p) {
		if (isPerCycle(p))
			return draw(p, position(p, loop));
		double a = draw(p, position(p, tick));
		return glide > 0.0 ? a + (draw(p, position(p, tick + 1)) - a) * glide : a;
	};
	int steps = t.steps();
	for (int i = 0; i < kNumTrackParams; i++) {
		TrackParam p = TrackParam(i);
		if (amount[p] == 0.f || !randomisable(p))
			continue;
		double offset = amount[p] / 100.0 * value(p); // -1..1
		const ParamInfo& info = kTrackParams[p];
		float base = t.values[p];
		switch (p) {
			case TP_PULSES: t.set(p, base + float(std::lround(offset * steps))); break;
			case TP_ROTATE: {
				// Wraps around the steps rather than piling up at the ends.
				int r = (int(base) + int(std::lround(offset * steps))) % steps;
				t.values[p] = float(r < 0 ? r + steps : r);
				break;
			}
			case TP_REPEATS: {
				// Random repeats stay finite unless the knob itself is at infinite.
				float v = clampParam(p, base + float(offset * (info.max - info.min)));
				t.values[p] = base < info.max ? std::min(v, info.max - 1) : v;
				break;
			}
			case TP_PROBABILITY: {
				// Adds skip chance; the knob's side (or the amount's when the knob is
				// centred) keeps choosing all notes vs pulses only.
				float sign = base != 0.f ? (base > 0 ? 1.f : -1.f) : (amount[p] > 0 ? 1.f : -1.f);
				float skip = std::min(std::fabs(base) + float(std::fabs(offset) * 100.0), 100.f);
				t.values[p] = sign * skip;
				break;
			}
			default: t.set(p, base + float(offset * (info.max - info.min))); break;
		}
	}
}

void Pattern::setBase(int track, TrackParam p, float v) {
	Track& t = tracks[track];
	float old = t.values[p];
	t.set(p, v);
	if (t.values[p] != old)
		cycles[track].shift(p, t.values[p] - old);
}

namespace {

bool sameTrack(const Track& a, const Track& b) {
	for (int p = 0; p < kNumTrackParams; p++) {
		if (a.values[p] != b.values[p])
			return false;
	}
	if (a.poolSize != b.poolSize)
		return false;
	for (int i = 0; i < a.poolSize; i++) {
		if (a.pool[i] != b.pool[i])
			return false;
	}
	return a.manualHits == b.manualHits && a.removedHits == b.removedHits && a.repeatTail == b.repeatTail
	       && a.groove == b.groove && a.grooveLength == b.grooveLength && a.swingSubdivision == b.swingSubdivision
	       && a.scale == b.scale && a.root == b.root && a.userScale == b.userScale && a.style == b.style
	       && a.mono == b.mono && a.voicingDown == b.voicingDown && a.phrase == b.phrase
	       && a.phraseLength == b.phraseLength && a.muted == b.muted && a.cc == b.cc && a.ccNumber == b.ccNumber;
}

bool sameCycle(const CycleOverrides& a, const CycleOverrides& b) {
	if (a.overridden != b.overridden || a.randomOverridden != b.randomOverridden || a.ownHits != b.ownHits
	    || a.muted != b.muted)
		return false;
	for (int p = 0; p < kNumTrackParams; p++) {
		if (a.has(TrackParam(p)) && a.values[p] != b.values[p])
			return false;
		if (a.hasRandom(TrackParam(p)) && a.randomAmounts[p] != b.randomAmounts[p])
			return false;
	}
	if (a.ownTonal != b.ownTonal)
		return false;
	if (a.ownTonal) {
		const Track &x = a.tonal, &y = b.tonal;
		if (x.scale != y.scale || x.root != y.root || x.userScale != y.userScale || x.poolSize != y.poolSize
		    || !std::equal(x.pool, x.pool + x.poolSize, y.pool))
			return false;
	}
	if (a.locks.count != b.locks.count || a.locks.poolCount != b.locks.poolCount)
		return false;
	for (int i = 0; i < a.locks.poolCount; i++) {
		const PoolLock* o = b.locks.poolAt(a.locks.poolStep[i]);
		if (!o || !(*o == a.locks.pool[i]))
			return false;
	}
	for (int i = 0; i < a.locks.count; i++) {
		TrackParam p = TrackParam(a.locks.param[i]);
		if (!b.locks.has(a.locks.step[i], p) || b.locks.get(a.locks.step[i], p) != a.locks.value[i])
			return false;
	}
	return !a.ownHits || (a.manualHits == b.manualHits && a.removedHits == b.removedHits);
}

bool sameLocks(const StepLocks& a, const StepLocks& b) {
	for (int s = 0; s < kMaxSteps; s++) {
		if (a.mask[s] != b.mask[s] || !(a.pool[s] == b.pool[s]))
			return false;
		for (int p = 0; p < kNumTrackParams; p++) {
			if (a.has(s, TrackParam(p)) && a.values[s][p] != b.values[s][p])
				return false;
		}
	}
	return true;
}

bool sameRandom(const TrackRandom& a, const TrackRandom& b) {
	for (int p = 0; p < kNumTrackParams; p++) {
		if (a.amount[p] != b.amount[p])
			return false;
	}
	return a.perCycle == b.perCycle && a.variance == b.variance && a.rate == b.rate && a.seed == b.seed
	       && a.phase == b.phase && a.probPhase == b.probPhase && a.slew == b.slew && a.locked == b.locked
	       && (!a.locked || (a.lockLoop == b.lockLoop && a.lockTick == b.lockTick));
}

} // namespace

bool lockable(TrackParam p) {
	return p != TP_STEPS && p != TP_PULSES && p != TP_ROTATE && p != TP_DIVISION && p != TP_LENGTH;
}

void PoolLock::set(const int* in, int n) {
	// Insertion into a sorted list, skipping duplicates (n <= kMaxPool).
	size = 0;
	for (int i = 0; i < std::clamp(n, 0, kMaxPool); i++) {
		uint8_t v = uint8_t(std::clamp(in[i], 0, 127));
		int k = 0;
		while (k < size && notes[k] < v)
			k++;
		if (k < size && notes[k] == v)
			continue;
		for (int j = size; j > k; j--)
			notes[j] = notes[j - 1];
		notes[k] = v;
		size++;
	}
}

void PoolLock::apply(Track& t) const {
	if (!size)
		return;
	for (int i = 0; i < size; i++)
		t.pool[i] = notes[i];
	t.poolSize = size;
}

bool PoolLock::operator==(const PoolLock& o) const {
	return size == o.size && std::equal(notes, notes + size, o.notes);
}

void StepLocks::setPool(int pos, const int* notes, int n) {
	if (pos < 0 || pos >= kMaxSteps)
		return;
	pool[pos].set(notes, n);
	StepMask bit = StepMask(1) << pos;
	pools = pool[pos].size ? pools | bit : pools & ~bit;
}

void StepLocks::set(int pos, TrackParam p, float v) {
	if (pos < 0 || pos >= kMaxSteps || !lockable(p))
		return;
	mask[pos] |= 1u << p;
	values[pos][p] = clampParam(p, v);
	used |= 1u << p;
}

void StepLocks::clear(int pos, TrackParam p) {
	if (pos < 0 || pos >= kMaxSteps)
		return;
	mask[pos] &= ~(1u << p);
	updateUsed();
}

void StepLocks::clearStep(int pos) {
	if (pos < 0 || pos >= kMaxSteps)
		return;
	mask[pos] = 0;
	setPool(pos, nullptr, 0);
	updateUsed();
}

void StepLocks::copyStep(int pos, uint32_t m, const float* v, const PoolLock& p) {
	if (pos < 0 || pos >= kMaxSteps)
		return;
	mask[pos] = 0;
	for (int i = 0; i < kNumTrackParams; i++) {
		if (m & (1u << i))
			set(pos, TrackParam(i), v[i]);
	}
	pool[pos] = p;
	StepMask bit = StepMask(1) << pos;
	pools = p.size ? pools | bit : pools & ~bit;
	updateUsed();
}

void StepLocks::apply(Track& t, int pos) const {
	for (int p = 0; p < kNumTrackParams; p++) {
		if (has(pos, TrackParam(p)))
			t.values[p] = values[pos][p];
	}
	pool[pos].apply(t);
}

int CycleLocks::find(int pos, TrackParam p) const {
	for (int i = 0; i < count; i++) {
		if (step[i] == pos && param[i] == p)
			return i;
	}
	return -1;
}

bool CycleLocks::set(int pos, TrackParam p, float v) {
	if (pos < 0 || pos >= kMaxSteps || !lockable(p))
		return false;
	int i = find(pos, p);
	if (i < 0) {
		if (count >= kMax)
			return false;
		i = count++;
		step[i] = uint8_t(pos);
		param[i] = uint8_t(p);
	}
	value[i] = clampParam(p, v);
	update();
	return true;
}

void CycleLocks::clear(int pos, TrackParam p) {
	int i = find(pos, p);
	if (i < 0)
		return;
	count--;
	step[i] = step[count];
	param[i] = param[count];
	value[i] = value[count];
	update();
}

void CycleLocks::clearStep(int pos) {
	for (int i = count - 1; i >= 0; i--) {
		if (step[i] == pos)
			clear(pos, TrackParam(param[i]));
	}
	setPool(pos, nullptr, 0);
}

int CycleLocks::findPool(int pos) const {
	for (int i = 0; i < poolCount; i++) {
		if (poolStep[i] == pos)
			return i;
	}
	return -1;
}

const PoolLock* CycleLocks::poolAt(int pos) const {
	int i = findPool(pos);
	return i >= 0 ? &pool[i] : nullptr;
}

bool CycleLocks::setPool(int pos, const int* notes, int n) {
	if (pos < 0 || pos >= kMaxSteps)
		return false;
	int i = findPool(pos);
	PoolLock p;
	p.set(notes, n);
	if (!p.size) {
		if (i >= 0) {
			poolCount--;
			poolStep[i] = poolStep[poolCount];
			pool[i] = pool[poolCount];
		}
	}
	else {
		if (i < 0) {
			if (poolCount >= kMaxPools)
				return false;
			i = poolCount++;
			poolStep[i] = uint8_t(pos);
		}
		pool[i] = p;
	}
	update();
	return true;
}

void CycleLocks::apply(Track& t, int pos) const {
	for (int i = 0; i < count; i++) {
		if (step[i] == pos)
			t.values[param[i]] = value[i];
	}
	if (const PoolLock* p = poolAt(pos))
		p->apply(t);
}

void CycleLocks::update() {
	steps = 0;
	used = 0;
	for (int i = 0; i < count; i++) {
		steps |= StepMask(1) << step[i];
		used |= 1u << param[i];
	}
	for (int i = 0; i < poolCount; i++)
		steps |= StepMask(1) << poolStep[i];
}

void StepLocks::updateUsed() {
	used = 0;
	for (uint32_t m : mask)
		used |= m;
}

bool Pattern::trackUsed(int i) const {
	static const Track kTrack;
	static const CycleOverrides kCycle;
	static const StepLocks kLocks;
	TrackRandom rnd;
	rnd.reset(i);
	if (!sameTrack(tracks[i], kTrack) || !sameRandom(random[i], rnd) || !sameLocks(locks[i], kLocks)
	    || cycles[i].count != 1)
		return true;
	for (const CycleOverrides& c : cycles[i].cycles) {
		if (!sameCycle(c, kCycle))
			return true;
	}
	return false;
}

bool operator==(const Pattern& a, const Pattern& b) {
	for (int i = 0; i < kNumTracks; i++) {
		if (!sameTrack(a.tracks[i], b.tracks[i]) || !sameRandom(a.random[i], b.random[i]) || !sameLocks(a.locks[i], b.locks[i])
		    || a.cycles[i].count != b.cycles[i].count)
			return false;
		for (int c = 0; c < kMaxCycles; c++) {
			if (!sameCycle(a.cycles[i].cycles[c], b.cycles[i].cycles[c]))
				return false;
		}
	}
	return true;
}

void HarmonyUndo::before(int track, int slot, const Track& t) {
	if (track < 0 || track >= kNumTracks)
		return;
	PoolLock now;
	now.set(t.pool, t.poolSize);
	// A new run whenever this isn't where Harmony left off: another edit moved the pool, the
	// target changed, or nothing is held yet.
	if (!held[track] || slotOf[track] != slot || !(now == last[track])) {
		base[track] = now;
		slotOf[track] = slot;
		held[track] = true;
	}
}

void HarmonyUndo::after(int track, const Track& t) {
	if (track < 0 || track >= kNumTracks)
		return;
	last[track].set(t.pool, t.poolSize);
}

bool HarmonyUndo::has(int track, int slot) const {
	return track >= 0 && track < kNumTracks && held[track] && slotOf[track] == slot && base[track].size > 0;
}

bool HarmonyUndo::restore(int track, int slot, Track& t) {
	if (!has(track, slot))
		return false;
	base[track].apply(t);
	held[track] = false;
	return true;
}

} // namespace talea
