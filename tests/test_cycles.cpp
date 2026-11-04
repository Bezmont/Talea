#include "Test.hpp"
#include "core/Engine.hpp"
#include "core/State.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace talea;

namespace {

struct On {
	double time;
	int note;
};

/** Note-ons of track 0 in (from, to], in windows of `window`. */
std::vector<On> ons(Engine& e, double from, double to, double window = 0.05) {
	std::vector<On> out;
	EventList list;
	for (int i = 0;; i++) {
		double a = from + i * window, b = from + (i + 1) * window;
		if (a >= to - 1e-9)
			break;
		list.clear();
		e.process(a, b >= to - 1e-9 ? to : b, list);
		for (const NoteEvent& n : list) {
			if (n.on && n.track == 0)
				out.push_back({n.time, n.note});
		}
	}
	return out;
}

std::vector<double> times(const std::vector<On>& notes) {
	std::vector<double> out;
	for (const On& n : notes)
		out.push_back(n.time);
	return out;
}

bool sameTimes(const std::vector<double>& a, const std::vector<double>& b) {
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); i++) {
		if (std::fabs(a[i] - b[i]) > 1e-9)
			return false;
	}
	return true;
}

/** Track 0: a note every beat, two cycles; cycle 2 plays only 2 pulses. */
void twoCycles(Engine& e) {
	e.pattern.tracks[0].set(TP_PULSES, 4);
	e.pattern.cycles[0].count = 2;
	e.pattern.cycles[0].set(1, TP_PULSES, 2);
}

} // namespace

TEST_CASE(cycles_take_turns_each_loop) {
	for (double window : {0.05, 0.7, 3.0}) {
		Engine e;
		twoCycles(e);
		CHECK(sameTimes(times(ons(e, -1e-9, 12.0, window)), {0, 1, 2, 3, 4, 6, 8, 9, 10, 11, 12}));
	}
}

TEST_CASE(cycles_with_different_loop_lengths) {
	Engine e;
	twoCycles(e);
	e.pattern.cycles[0].set(1, TP_STEPS, 8); // cycle 2: 8 steps = 2 quarters, 2 pulses
	e.pattern.cycles[0].set(1, TP_PULSES, 4);
	CHECK(sameTimes(times(ons(e, -1e-9, 7.0)), {0, 1, 2, 3, 4, 4.5, 5, 5.5, 6, 7}));
	CHECK_EQ(e.cycleAt(0, 3.9), 0);
	CHECK_EQ(e.cycleAt(0, 4.0), 1);
	CHECK_EQ(e.cycleAt(0, 6.0), 0);
	CHECK_EQ(e.stepAt(0, 5.0), 4); // cycle 2 restarts the step count at 4.0
	CHECK(std::fabs(e.patternQuarters() - 6.0) < 1e-9);
}

TEST_CASE(cycle_overrides_change_notes) {
	Engine e;
	twoCycles(e);
	e.pattern.cycles[0].set(1, TP_PITCH, 12);
	std::vector<On> notes = ons(e, -1e-9, 8.0);
	CHECK_EQ(notes.size(), size_t(7)); // the window includes 8.0
	CHECK(notes[0].note == 60 && notes[4].note == 72 && notes[5].note == 72 && notes[6].note == 60);
}

TEST_CASE(muted_cycle_is_silent) {
	Engine e;
	twoCycles(e);
	e.pattern.cycles[0].cycles[1].muted = true;
	CHECK(sameTimes(times(ons(e, -1e-9, 12.0)), {0, 1, 2, 3, 8, 9, 10, 11})); // 12.0 starts the muted cycle again
}

TEST_CASE(solo_cycle_plays_alone) {
	Engine e;
	twoCycles(e);
	e.soloCycle[0] = 1;
	CHECK(sameTimes(times(ons(e, -1e-9, 8.0)), {0, 2, 4, 6, 8}));
	CHECK_EQ(e.cycleAt(0, 5.0), 1);
}

TEST_CASE(base_edits_move_overrides) {
	Pattern p;
	p.cycles[0].count = 3;
	p.cycles[0].set(1, TP_PULSES, 2);
	p.cycles[0].set(2, TP_PULSES, 15);
	p.setBase(0, TP_PULSES, 3); // +3
	CHECK_EQ(p.tracks[0].getInt(TP_PULSES), 3);
	CHECK_EQ(p.cycles[0].value(p.tracks[0], 1, TP_PULSES), 5.f);
	CHECK_EQ(p.cycles[0].value(p.tracks[0], 2, TP_PULSES), 18.f);
	CHECK_EQ(p.cycles[0].value(p.tracks[0], 0, TP_PULSES), 3.f); // no override: the base
	p.setBase(0, TP_PULSES, 0);
	CHECK_EQ(p.cycles[0].value(p.tracks[0], 1, TP_PULSES), 2.f);
	p.cycles[0].clear(1, TP_PULSES);
	CHECK(p.cycles[0].cycles[1].empty());
}

TEST_CASE(cycle_step_edits_are_its_own) {
	Engine e;
	twoCycles(e);
	TrackCycles& cyc = e.pattern.cycles[0];
	cyc.clear(1, TP_PULSES);         // both cycles: a note every beat
	cyc.toggleStep(e.pattern.tracks[0], 1, 2); // cycle 2 adds step 3 (0.5)
	cyc.toggleStep(e.pattern.tracks[0], 1, 4); // ... and drops step 5 (1.0)
	CHECK(cyc.cycles[1].ownHits && cyc.cycles[1].modified());
	CHECK_EQ(e.pattern.tracks[0].manualHits, StepMask(0)); // the track is untouched
	CHECK(sameTimes(times(ons(e, -1e-9, 8.0)), {0, 1, 2, 3, 4, 4.5, 6, 7, 8}));
	// A track edit no longer reaches the cycle with its own hits.
	e.pattern.tracks[0].toggleStep(1);
	CHECK(sameTimes(times(ons(e, 8.0, 16.0)), {8.25, 9, 10, 11, 12, 12.5, 14, 15, 16}));
}

TEST_CASE(cycles_state_round_trip) {
	Pattern a;
	a.cycles[3].count = 5;
	a.cycles[3].set(4, TP_STEPS, 12);
	a.cycles[3].set(4, TP_RAMP, -40);
	a.cycles[3].cycles[2].muted = true;
	a.cycles[3].toggleStep(a.tracks[3], 4, 5);
	Pattern b;
	CHECK(patternFromJson(patternToJson(a), b));
	const TrackCycles& c = b.cycles[3];
	CHECK_EQ(c.count, 5);
	CHECK(c.cycles[4].has(TP_STEPS) && c.cycles[4].has(TP_RAMP) && !c.cycles[4].has(TP_PULSES));
	CHECK_EQ(c.cycles[4].values[TP_RAMP], -40.f);
	CHECK(c.cycles[2].muted && c.cycles[1].empty());
	CHECK(c.cycles[4].ownHits && c.cycles[4].manualHits == a.cycles[3].cycles[4].manualHits);
	CHECK_EQ(b.cycles[0].count, 1);
}

TEST_CASE(cycle_tonal_overrides_scale_and_pool) {
	Pattern a;
	Track& base = a.tracks[1];
	TrackCycles& cyc = a.cycles[1];
	cyc.count = 2;
	Track& tn = cyc.tonalEdit(base, 1);
	CHECK(cyc.cycles[1].ownTonal && cyc.cycles[1].modified());
	tn.root = 2;
	tn.togglePoolNote(67);
	CHECK_EQ(cyc.resolve(base, 1).root, 2);
	CHECK_EQ(cyc.resolve(base, 1).poolSize, 2);
	CHECK_EQ(cyc.resolve(base, 0).root, 0);
	CHECK_EQ(cyc.resolve(base, 0).poolSize, 1);
	Pattern b;
	CHECK(patternFromJson(patternToJson(a), b));
	CHECK(a == b);
	CHECK(b.cycles[1].cycles[1].ownTonal && b.cycles[1].cycles[1].tonal.pool[1] == 67);
	cyc.cycles[1].clearChanges();
	CHECK(!cyc.cycles[1].ownTonal);
}

TEST_CASE(pattern_track_used) {
	Pattern a;
	for (int i = 0; i < kNumTracks; i++)
		CHECK(!a.trackUsed(i));
	a.tracks[3].set(TP_PULSES, 2);
	a.cycles[5].count = 2;
	a.locks[7].set(1, TP_VELOCITY, 10);
	a.random[9].amount[TP_PITCH] = 5;
	CHECK(a.trackUsed(3) && a.trackUsed(5) && a.trackUsed(7) && a.trackUsed(9));
	CHECK(!a.trackUsed(4));
}
