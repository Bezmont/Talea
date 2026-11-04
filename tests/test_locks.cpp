#include "Test.hpp"
#include "core/Engine.hpp"
#include "core/State.hpp"

#include <cmath>
#include <vector>

using namespace talea;

namespace {

struct On {
	double time;
	int velocity;
	int note;
};

/** Note-ons of track 0 in (from, to]. */
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
				out.push_back({n.time, n.velocity, n.note});
		}
	}
	return out;
}

/** Track 0: 4 pulses of 16 steps (steps 0, 4, 8, 12) at velocity 64. */
void fourPulses(Engine& e) {
	e.pattern.tracks[0].set(TP_PULSES, 4);
	e.pattern.tracks[0].set(TP_VELOCITY, 64);
}

} // namespace

TEST_CASE(locks_replace_values_on_their_step) {
	Engine e;
	fourPulses(e);
	e.pattern.locks[0].set(4, TP_VELOCITY, 120);
	e.pattern.locks[0].set(4, TP_PITCH, 7);
	std::vector<On> notes = ons(e, -1e-9, 4.0 - 1e-6);
	CHECK_EQ(notes.size(), size_t(4));
	CHECK_EQ(notes[0].velocity, 64);
	CHECK_EQ(notes[1].velocity, 120);
	CHECK_EQ(notes[1].note, notes[0].note + 7);
	CHECK_EQ(notes[2].velocity, 64);
	CHECK_EQ(notes[2].note, notes[0].note);
}

TEST_CASE(locks_move_with_rotate) {
	Engine e;
	fourPulses(e);
	e.pattern.tracks[0].set(TP_ROTATE, 2); // hits on 2, 6, 10, 14
	e.pattern.locks[0].set(4, TP_VELOCITY, 100);
	std::vector<On> notes = ons(e, -1e-9, 4.0 - 1e-6);
	CHECK_EQ(notes.size(), size_t(4));
	CHECK(std::fabs(notes[1].time - 1.5) < 1e-9);
	CHECK_EQ(notes[1].velocity, 100);
	CHECK_EQ(notes[0].velocity, 64);
}

TEST_CASE(locks_reach_repeats_and_timing) {
	Engine e;
	fourPulses(e);
	e.pattern.locks[0].set(0, TP_REPEATS, 2); // two repeats after step 1 only
	e.pattern.locks[0].set(8, TP_DELAY, 50);  // step 9 half a step late
	std::vector<On> notes = ons(e, -1e-9, 4.0 - 1e-6);
	CHECK_EQ(notes.size(), size_t(6));
	CHECK(notes[1].time > 0.0 && notes[2].time < 1.0);
	CHECK(std::fabs(notes[3].time - 1.0) < 1e-9);
	CHECK(std::fabs(notes[4].time - 2.0) > 0.05 && std::fabs(notes[4].time - 2.0) <= 0.25);
}

TEST_CASE(locks_skip_loop_shape_params_and_round_trip) {
	Pattern a;
	StepLocks& l = a.locks[3];
	l.set(5, TP_PULSES, 3);
	l.set(5, TP_STEPS, 3);
	CHECK(!l.any(5));
	l.set(5, TP_SUSTAIN, 40);
	l.set(63, TP_PROBABILITY, -20);
	l.set(64, TP_VELOCITY, 1); // out of range: ignored
	CHECK(l.used == (1u << TP_SUSTAIN | 1u << TP_PROBABILITY));
	Pattern b;
	CHECK(a != b);
	CHECK(patternFromJson(patternToJson(a), b));
	CHECK(a == b);
	CHECK(b.locks[3].has(5, TP_SUSTAIN) && b.locks[3].values[5][TP_SUSTAIN] == 40.f);
	l.clear(63, TP_PROBABILITY);
	CHECK(l.used == 1u << TP_SUSTAIN);
	float v[kNumTrackParams] = {};
	v[TP_VELOCITY] = 90;
	l.copyStep(5, 1u << TP_VELOCITY | 1u << TP_ROTATE, v, PoolLock());
	CHECK(l.mask[5] == 1u << TP_VELOCITY);
	l.clearStep(5);
	CHECK(l.used == 0u);
}

TEST_CASE(cycle_locks_play_in_their_cycle_only) {
	Engine e;
	fourPulses(e);
	TrackCycles& cyc = e.pattern.cycles[0];
	cyc.count = 2;
	e.pattern.locks[0].set(4, TP_VELOCITY, 100);    // every cycle
	CHECK(cyc.cycles[1].locks.set(4, TP_VELOCITY, 30)); // cycle 2 over the track
	CHECK(cyc.cycles[1].locks.set(8, TP_VELOCITY, 20)); // cycle 2 only
	CHECK(cyc.cycles[1].modified());
	std::vector<On> notes = ons(e, -1e-9, 8.0 - 1e-6);
	CHECK_EQ(notes.size(), size_t(8));
	CHECK_EQ(notes[1].velocity, 100);
	CHECK_EQ(notes[2].velocity, 64);
	CHECK_EQ(notes[5].velocity, 30);
	CHECK_EQ(notes[6].velocity, 20);
	CHECK_EQ(notes[4].velocity, 64);
}

TEST_CASE(cycle_locks_edit_and_round_trip) {
	CycleLocks l;
	CHECK(!l.set(3, TP_ROTATE, 2));
	CHECK(l.set(3, TP_PITCH, 5));
	CHECK(l.set(3, TP_SUSTAIN, 70));
	CHECK(l.set(9, TP_PITCH, -3));
	CHECK(l.set(3, TP_PITCH, 6)); // replaces
	CHECK_EQ(l.count, 3);
	CHECK(l.any(3) && l.any(9) && !l.any(4));
	CHECK_EQ(l.get(3, TP_PITCH), 6.f);
	l.clearStep(3);
	CHECK_EQ(l.count, 1);
	CHECK(!l.any(3) && l.used == 1u << TP_PITCH);
	for (int i = 0; i < CycleLocks::kMax; i++)
		l.set(i % kMaxSteps, TrackParam(TP_REPEATS + i / kMaxSteps), 1);
	CHECK(!l.set(10, TP_RANGE, 1)); // full

	Pattern a;
	a.cycles[2].count = 2;
	a.cycles[2].cycles[1].locks.set(7, TP_VELOCITY, 99);
	a.cycles[2].cycles[1].locks.set(0, TP_DELAY, -40);
	Pattern b;
	CHECK(a != b);
	CHECK(patternFromJson(patternToJson(a), b));
	CHECK(a == b);
	a.cycles[2].cycles[1].clearChanges();
	CHECK(a.cycles[2].cycles[1].empty());
}

TEST_CASE(cv_mod_offsets_on_top) {
	Engine e;
	fourPulses(e);
	e.pattern.locks[0].set(4, TP_VELOCITY, 100);
	e.mod[0][TP_VELOCITY] = 20;
	e.mod[0][TP_STEPS] = 5; // ignored: sets the loop length
	std::vector<On> notes = ons(e, -1e-9, 4.0 - 1e-6);
	CHECK_EQ(notes.size(), size_t(4));
	CHECK_EQ(notes[0].velocity, 84);
	CHECK_EQ(notes[1].velocity, 120);
	e.mod[0][TP_VELOCITY] = 0;
	e.mod[0][TP_ROTATE] = 17; // wraps: one step later
	notes = ons(e, 4.0 - 1e-6, 8.0 - 1e-6);
	CHECK_EQ(notes.size(), size_t(4));
	CHECK(std::fabs(notes[0].time - 4.25) < 1e-9);
}

TEST_CASE(settings_at_shows_live_values) {
	Engine e;
	fourPulses(e);
	e.pattern.locks[0].set(4, TP_VELOCITY, 100);
	e.mod[0][TP_VELOCITY] = 10;
	CHECK_EQ(e.settingsAt(0, 0.1).getInt(TP_VELOCITY), 74);
	CHECK_EQ(e.settingsAt(0, 1.1).getInt(TP_VELOCITY), 110); // step 5 is locked
	CHECK_EQ(e.pattern.tracks[0].getInt(TP_VELOCITY), 64);
}
