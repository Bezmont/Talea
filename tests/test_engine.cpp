#include "Test.hpp"
#include "core/Engine.hpp"
#include "core/State.hpp"

#include <cmath>
#include <vector>

using namespace talea;

namespace {

// Index into a value list by name, so tests read as musical values.
template <size_t N>
float indexOf(const NamedValue (&list)[N], const char* name) {
	for (size_t i = 0; i < N; i++) {
		if (std::string(list[i].name) == name)
			return float(i);
	}
	return -1.f;
}

std::vector<NoteEvent> run(Engine& e, double from, double to, double window) {
	std::vector<NoteEvent> all;
	EventList list;
	for (double t = from; t < to - 1e-12; t += window) {
		list.clear();
		e.process(t, std::min(t + window, to), list);
		all.insert(all.end(), list.begin(), list.end());
	}
	return all;
}

std::vector<double> onTimes(const std::vector<NoteEvent>& events, int track = -1) {
	std::vector<double> out;
	for (const NoteEvent& e : events) {
		if (e.on && (track < 0 || e.track == track))
			out.push_back(e.time);
	}
	return out;
}

bool near(double a, double b) {
	return std::fabs(a - b) < 1e-6;
}

} // namespace

TEST_CASE(engine_default_pattern_is_silent) {
	Engine e;
	CHECK_EQ(int(run(e, -1e-9, 16.0, 1.0).size()), 0);
}

TEST_CASE(engine_four_on_the_floor) {
	Engine e;
	Track& t = e.pattern.tracks[0];
	t.set(TP_PULSES, 4); // 16 steps of 1/16, 4 pulses = every quarter
	std::vector<double> ons = onTimes(run(e, -1e-9, 4.0 - 1e-6, 4.0));
	CHECK_EQ(int(ons.size()), 4);
	for (int i = 0; i < 4 && i < int(ons.size()); i++)
		CHECK(near(ons[i], i));
	// (from, to]: a window ending exactly on the bar 2 downbeat includes it
	CHECK_EQ(int(onTimes(run(e, 4.0 - 1e-6, 4.0, 1.0)).size()), 1);
}

TEST_CASE(engine_window_size_does_not_change_events) {
	auto build = [](Engine& e) {
		e.pattern.tracks[0].set(TP_PULSES, 5);
		e.pattern.tracks[1].set(TP_STEPS, 7);
		e.pattern.tracks[1].set(TP_PULSES, 3);
		e.pattern.tracks[1].set(TP_DIVISION, indexOf(kDivisions, "1/8t"));
		e.pattern.tracks[2].set(TP_PULSES, 16);
		e.pattern.tracks[2].set(TP_LENGTH, indexOf(kLengths, "1/2"));
		e.pattern.tracks[2].set(TP_SUSTAIN, 100);
	};
	Engine a, b;
	build(a);
	build(b);
	std::vector<NoteEvent> big = run(a, -1e-9, 32.0, 32.0);
	// ~1 sample at 120 BPM / 48 kHz
	std::vector<NoteEvent> small = run(b, -1e-9, 32.0, 2.0 / 48000.0);
	CHECK_EQ(big.size(), small.size());
	for (size_t i = 0; i < big.size() && i < small.size(); i++) {
		CHECK(near(big[i].time, small[i].time));
		CHECK_EQ(big[i].track, small[i].track);
		CHECK_EQ(big[i].on, small[i].on);
	}
}

TEST_CASE(engine_length_restarts_steps) {
	Engine e;
	Track& t = e.pattern.tracks[0];
	t.set(TP_PULSES, 1); // hit on step 0 only
	t.set(TP_LENGTH, indexOf(kLengths, "1/4"));
	// Step 0 recurs every quarter instead of every 16 steps.
	std::vector<double> ons = onTimes(run(e, -1e-9, 4.0 - 1e-6, 4.0));
	CHECK_EQ(int(ons.size()), 4);
	CHECK_EQ(e.stepAt(0, 0.0), 0);
	CHECK_EQ(e.stepAt(0, 0.75), 3);
	CHECK_EQ(e.stepAt(0, 1.0), 0);
	CHECK_EQ(e.stepAt(0, -0.5), -1);
}

TEST_CASE(engine_infinite_length_wraps_steps) {
	Engine e;
	Track& t = e.pattern.tracks[0];
	t.set(TP_STEPS, 5);
	t.set(TP_PULSES, 1);
	t.set(TP_LENGTH, indexOf(kLengths, "∞"));
	std::vector<double> ons = onTimes(run(e, -1e-9, 5.0 - 1e-6, 1.0));
	// every 5 sixteenths = 1.25 quarters: 0, 1.25, 2.5, 3.75
	CHECK_EQ(int(ons.size()), 4);
	CHECK(ons.size() == 4 && near(ons[3], 3.75));
	CHECK_EQ(e.stepAt(0, 1.25), 0);
	CHECK_EQ(e.stepAt(0, 1.5), 1);
}

TEST_CASE(engine_triplets_fill_bar_exactly) {
	Engine e;
	Track& t = e.pattern.tracks[0];
	t.set(TP_PULSES, 16);
	t.set(TP_DIVISION, indexOf(kDivisions, "1/16t"));
	t.set(TP_LENGTH, indexOf(kLengths, "1 bar"));
	// 24 triplet sixteenths per bar, no doubled or dropped step at the bar line.
	CHECK_EQ(int(onTimes(run(e, -1e-9, 8.0 - 1e-6, 0.37)).size()), 48);
}

TEST_CASE(engine_sustain_sets_gate_length) {
	Engine e;
	Track& t = e.pattern.tracks[0];
	t.set(TP_PULSES, 4);
	std::vector<NoteEvent> ev = run(e, -1e-9, 1.0 - 1e-6, 0.01);
	CHECK_EQ(int(ev.size()), 2);
	if (ev.size() == 2) {
		CHECK(ev[0].on && near(ev[0].time, 0.0));
		CHECK(!ev[1].on && near(ev[1].time, 0.25)); // 50 % = one 1/16 division
	}
}

TEST_CASE(engine_overlapping_notes_retrigger) {
	Engine e;
	Track& t = e.pattern.tracks[0];
	t.set(TP_PULSES, 16);
	t.set(TP_SUSTAIN, 100); // two divisions: each note overlaps the next
	std::vector<NoteEvent> ev = run(e, -1e-9, 0.25 + 1e-6, 1.0);
	// on@0, off@0.25 then on@0.25
	CHECK_EQ(int(ev.size()), 3);
	if (ev.size() == 3) {
		CHECK(ev[1].time == ev[2].time && !ev[1].on && ev[2].on);
	}
}

TEST_CASE(engine_all_notes_off) {
	Engine e;
	e.pattern.tracks[3].set(TP_PULSES, 1);
	EventList list;
	e.process(-1e-9, 0.1, list);
	CHECK_EQ(list.size, 1);
	list.clear();
	e.allNotesOff(0.1, list);
	CHECK_EQ(list.size, 1);
	CHECK(list.size == 1 && !list.events[0].on && list.events[0].track == 3);
	list.clear();
	e.process(0.1, 0.2, list); // the stale note-off is not sent again
	CHECK_EQ(list.size, 0);
}

TEST_CASE(engine_pitch_and_velocity) {
	Engine e;
	Track& t = e.pattern.tracks[0];
	t.set(TP_PULSES, 1);
	t.set(TP_PITCH, 7);
	t.set(TP_VELOCITY, 90);
	EventList list;
	e.process(-1e-9, 0.01, list);
	CHECK_EQ(list.size, 1);
	CHECK_EQ(list.events[0].note, 67);
	CHECK_EQ(list.events[0].velocity, 90);
}

TEST_CASE(engine_pattern_length) {
	Engine e;
	CHECK(near(e.patternQuarters(), 4.0)); // all tracks 16 x 1/16, Length infinite
	e.pattern.tracks[2].set(TP_STEPS, 32);
	CHECK(near(e.patternQuarters(), 8.0)); // an infinite Length loops over its steps
	e.pattern.tracks[5].set(TP_LENGTH, indexOf(kLengths, "4 bars"));
	CHECK(near(e.patternQuarters(), 16.0));
}

TEST_CASE(track_played_steps) {
	Track t;
	t.set(TP_STEPS, 32);
	CHECK_EQ(t.playedSteps(), 32); // Length infinite
	t.set(TP_LENGTH, indexOf(kLengths, "1 bar"));
	CHECK_EQ(t.playedSteps(), 16);
	t.set(TP_LENGTH, indexOf(kLengths, "2 bars"));
	CHECK_EQ(t.playedSteps(), 32);
	t.set(TP_DIVISION, indexOf(kDivisions, "1/16t")); // 1/6 quarter: 1 bar = 24 steps
	t.set(TP_LENGTH, indexOf(kLengths, "1 bar"));
	CHECK_EQ(t.playedSteps(), 24);
}

TEST_CASE(track_toggle_step) {
	Track t;
	t.set(TP_PULSES, 4); // x...x...x...x...
	t.toggleStep(2);     // empty -> manual hit
	CHECK(hasHit(t.hits(), 2));
	t.set(TP_PULSES, 2); // manual hits are sticky
	CHECK(hasHit(t.hits(), 2));
	t.toggleStep(2); // manual hit -> removed
	CHECK(!hasHit(t.hits(), 2));
	t.set(TP_PULSES, 4);
	StepMask before = t.hits();
	t.toggleStep(4); // generated hit -> only that step removed
	CHECK_EQ(t.pulses(), 4);
	CHECK_EQ(t.hits(), before & ~(StepMask(1) << 4));
	t.toggleStep(4); // removed generated hit -> restored
	CHECK_EQ(t.hits(), before);

	Track r;
	r.set(TP_ROTATE, 3);
	r.toggleStep(5); // played position 5 = unrotated 2
	CHECK_EQ(r.manualHits, StepMask(1) << 2);
	CHECK(hasHit(r.hits(), 5));
}

TEST_CASE(track_set_clamps_and_snaps) {
	Track t;
	t.set(TP_STEPS, 100);
	CHECK_EQ(t.steps(), 64);
	t.set(TP_PULSES, 3.6f);
	CHECK_EQ(t.get(TP_PULSES), 4.f);
	t.set(TP_RAMP, 33.3f); // continuous: not snapped
	CHECK(std::fabs(t.get(TP_RAMP) - 33.3f) < 1e-4);
	t.set(TP_STEPS, 8);
	t.set(TP_PULSES, 12);
	CHECK_EQ(t.pulses(), 8); // limited to Steps, stored value kept
	CHECK_EQ(t.get(TP_PULSES), 12.f);
}

TEST_CASE(state_round_trip) {
	Pattern a;
	a.tracks[0].set(TP_PULSES, 5);
	a.tracks[0].set(TP_RAMP, -42.5f);
	a.tracks[15].set(TP_LENGTH, indexOf(kLengths, "16 bars"));
	a.tracks[7].manualHits = (StepMask(1) << 63) | 5;
	a.tracks[7].removedHits = StepMask(1) << 40;
	a.tracks[3].repeatTail = true;
	std::string s = patternToJson(a);

	Pattern b;
	CHECK(patternFromJson(s, b));
	for (int i = 0; i < kNumTracks; i++) {
		for (int p = 0; p < kNumTrackParams; p++)
			CHECK_EQ(b.tracks[i].values[p], a.tracks[i].values[p]);
		CHECK_EQ(b.tracks[i].manualHits, a.tracks[i].manualHits);
		CHECK_EQ(b.tracks[i].removedHits, a.tracks[i].removedHits);
		CHECK_EQ(b.tracks[i].repeatTail, a.tracks[i].repeatTail);
	}
	CHECK_EQ(patternToJson(b), s);
}

TEST_CASE(state_rejects_garbage_and_tolerates_gaps) {
	Pattern p;
	p.tracks[0].set(TP_PULSES, 3);
	CHECK(!patternFromJson("not json", p));
	CHECK_EQ(p.tracks[0].pulses(), 0); // reset to defaults

	// Unknown keys ignored, missing keys default, out-of-range values clamped.
	CHECK(patternFromJson(R"({"version":1,"tracks":[{"steps":200,"bogus":1}]})", p));
	CHECK_EQ(p.tracks[0].steps(), 64);
	CHECK_EQ(p.tracks[0].get(TP_VELOCITY), 100.f);
}

// ------------------------------------------------------------------ repeats

namespace {

Engine repeatEngine(int pulses, int repeats, const char* time) {
	Engine e;
	Track& t = e.pattern.tracks[0];
	t.set(TP_PULSES, float(pulses));
	t.set(TP_REPEATS, float(repeats));
	t.set(TP_REPEAT_TIME, indexOf(kRepeatTimes, time));
	return e;
}

std::vector<int> onVelocities(const std::vector<NoteEvent>& events) {
	std::vector<int> out;
	for (const NoteEvent& e : events) {
		if (e.on)
			out.push_back(e.velocity);
	}
	return out;
}

} // namespace

TEST_CASE(repeats_follow_each_pulse) {
	Engine e = repeatEngine(1, 3, "1/16");
	std::vector<double> ons = onTimes(run(e, -1e-9, 4.0 - 1e-6, 0.1));
	CHECK_EQ(int(ons.size()), 4);
	for (int i = 0; i < 4 && i < int(ons.size()); i++)
		CHECK(near(ons[i], 0.25 * i));
}

TEST_CASE(repeats_choke_vs_tail) {
	// Pulses at 0, 2, 4, 6; repeats every dotted half (3 quarters).
	Engine choke = repeatEngine(8, 3, "1/2d");
	choke.pattern.tracks[0].set(TP_DIVISION, indexOf(kDivisions, "1/4"));
	choke.pattern.tracks[0].set(TP_STEPS, 2);
	choke.pattern.tracks[0].set(TP_PULSES, 1);
	choke.pattern.tracks[0].set(TP_LENGTH, indexOf(kLengths, "∞"));
	Engine tail = choke;
	tail.pattern.tracks[0].repeatTail = true;

	// Choke: every repeat is cut by the next pulse.
	CHECK_EQ(int(onTimes(run(choke, -1e-9, 8.0 - 1e-6, 0.3)).size()), 4);
	// Tail: 0, 2, 3 (rep), 4, 5 (rep), 6 (pulse + coincident rep, played once), 7 (rep)
	std::vector<double> ons = onTimes(run(tail, -1e-9, 8.0 - 1e-6, 0.3));
	CHECK_EQ(int(ons.size()), 7);
}

TEST_CASE(repeats_pace_shapes_spacing) {
	for (float pace : {100.f, -100.f}) {
		Engine e = repeatEngine(1, 6, "1/8");
		e.pattern.tracks[0].set(TP_PACE, pace);
		e.pattern.tracks[0].set(TP_STEPS, 64);
		e.pattern.tracks[0].set(TP_LENGTH, indexOf(kLengths, "4 bars"));
		std::vector<double> ons = onTimes(run(e, -1e-9, 16.0 - 1e-6, 0.5));
		CHECK_EQ(int(ons.size()), 7);
		for (size_t i = 2; i < ons.size(); i++) {
			double gap = ons[i] - ons[i - 1], before = ons[i - 1] - ons[i - 2];
			CHECK(pace > 0 ? gap > before : gap < before);
		}
		if (ons.size() >= 2)
			CHECK(near(ons[1], 0.5)); // first repeat is always one Time after the pulse
	}
}

TEST_CASE(repeats_ramp_velocity) {
	Engine down = repeatEngine(1, 4, "1/16");
	down.pattern.tracks[0].set(TP_RAMP, -100);
	std::vector<int> v = onVelocities(run(down, -1e-9, 1.0, 0.1));
	CHECK(v == std::vector<int>({100, 75, 50, 25, 1}));

	Engine up = repeatEngine(1, 4, "1/16");
	up.pattern.tracks[0].set(TP_RAMP, 100);
	v = onVelocities(run(up, -1e-9, 1.0, 0.1));
	CHECK(v == std::vector<int>({100, 25, 50, 75, 100}));
}

TEST_CASE(repeats_infinite_fill_until_pulse_returns) {
	Engine e = repeatEngine(1, kMaxRepeats + 1, "1/16");
	CHECK_EQ(e.pattern.tracks[0].repeats(), kInfiniteRepeats);
	std::vector<double> ons = onTimes(run(e, -1e-9, 8.0 - 1e-6, 0.37));
	CHECK_EQ(int(ons.size()), 32); // every 1/16, no doubled note at the bar line
}

TEST_CASE(repeats_window_size_and_reset) {
	auto build = [](Engine& e) {
		Track& t = e.pattern.tracks[0];
		t.set(TP_PULSES, 3);
		t.set(TP_REPEATS, 5);
		t.set(TP_REPEAT_TIME, indexOf(kRepeatTimes, "1/8t"));
		t.set(TP_PACE, 40);
		t.set(TP_RAMP, -30);
		t.repeatTail = true;
	};
	Engine a, b;
	build(a);
	build(b);
	std::vector<NoteEvent> big = run(a, -1e-9, 16.0, 16.0);
	std::vector<NoteEvent> small = run(b, -1e-9, 16.0, 2.0 / 48000.0);
	CHECK_EQ(big.size(), small.size());
	for (size_t i = 0; i < big.size() && i < small.size(); i++) {
		CHECK(near(big[i].time, small[i].time));
		CHECK_EQ(big[i].on, small[i].on);
		CHECK_EQ(big[i].velocity, small[i].velocity);
	}

	// Jumping back to the start drops the bursts in flight: the replay matches a fresh run.
	Engine fresh;
	build(fresh);
	std::vector<NoteEvent> again = run(a, -1e-9, 16.0, 16.0);
	std::vector<NoteEvent> first = run(fresh, -1e-9, 16.0, 16.0);
	int againOns = int(onTimes(again).size()), firstOns = int(onTimes(first).size());
	CHECK_EQ(againOns, firstOns);
}

// A host that computes ahead (Max for Live) rewinds the engine after an edit: loading the
// state saved after a window, then computing from there, must give what the engine would
// have played had it never run ahead, with or without an edit at that point.
TEST_CASE(engine_playback_save_and_load) {
	auto build = [](Engine& e) {
		e.pattern.tracks[0].set(TP_PULSES, 5);
		e.pattern.tracks[0].set(TP_SUSTAIN, 90);
		e.pattern.tracks[1].set(TP_PULSES, 3);
		e.pattern.tracks[1].set(TP_REPEATS, 5);
		e.pattern.tracks[1].set(TP_REPEAT_TIME, indexOf(kRepeatTimes, "1/8t"));
		e.pattern.tracks[1].set(TP_PACE, 40);
		e.pattern.tracks[1].repeatTail = true;
		e.pattern.tracks[2].set(TP_PULSES, 7);
		e.pattern.tracks[2].set(TP_TIMING, 50);
	};
	auto same = [](const std::vector<NoteEvent>& a, const std::vector<NoteEvent>& b) {
		if (a.size() != b.size())
			return false;
		for (size_t i = 0; i < a.size(); i++) {
			if (!near(a[i].time, b[i].time) || a[i].track != b[i].track || a[i].on != b[i].on ||
			    a[i].note != b[i].note || a[i].velocity != b[i].velocity)
				return false;
		}
		return true;
	};
	auto edit = [](Engine& e) {
		e.pattern.tracks[1].set(TP_PACE, -60);
		e.pattern.tracks[0].set(TP_PULSES, 9);
	};
	const double step = 0.25, eps = 1e-9, at = 37 * step; // mid-burst on track 1
	auto upTo = [&](Engine& e) { run(e, -eps, at - eps, step); };
	auto rest = [&](Engine& e) { return run(e, at - eps, at + 16 - eps, step); };
	auto runAhead = [&](Engine& e) { run(e, at - eps, at + 8 * step - eps, step); };

	Engine plain, ahead, stale;
	build(plain);
	build(ahead);
	build(stale);
	upTo(plain);
	upTo(ahead);
	upTo(stale);
	Engine::Playback saved;
	ahead.savePlayback(saved);
	runAhead(ahead);
	ahead.loadPlayback(saved);
	stale.savePlayback(saved);
	runAhead(stale); // not rewound: what loadPlayback prevents
	std::vector<NoteEvent> want = rest(plain);
	CHECK(!want.empty());
	CHECK(same(rest(ahead), want));
	CHECK(!same(rest(stale), want));

	Engine edited, ahead2;
	build(edited);
	build(ahead2);
	upTo(edited);
	upTo(ahead2);
	ahead2.savePlayback(saved);
	runAhead(ahead2);
	ahead2.loadPlayback(saved);
	edit(ahead2);
	edit(edited);
	CHECK(same(rest(ahead2), rest(edited)));
}
