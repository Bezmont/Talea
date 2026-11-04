#include "Test.hpp"
#include "core/Engine.hpp"
#include "core/State.hpp"

#include <vector>

using namespace talea;

namespace {

/** Note-ons of track 0 in (from, to]. */
std::vector<NoteEvent> ons(Engine& e, double from, double to) {
	std::vector<NoteEvent> out;
	EventList list;
	e.process(from, to, list);
	for (const NoteEvent& n : list) {
		if (n.on && n.track == 0)
			out.push_back(n);
	}
	return out;
}

/** Track 0: 4 pulses of 16 steps (steps 0, 4, 8, 12), pool C4 (chromatic scale). */
void fourPulses(Engine& e) {
	e.pattern.tracks[0].set(TP_PULSES, 4);
}

} // namespace

TEST_CASE(step_pool_replaces_the_track_pool) {
	Engine e;
	fourPulses(e);
	const int chord[] = {67, 60, 64, 64}; // unsorted, with a duplicate
	e.pattern.locks[0].setPool(4, chord, 4);
	CHECK(e.pattern.locks[0].any(4));
	std::vector<NoteEvent> v = ons(e, -1e-9, 4.0 - 1e-6);
	CHECK_EQ(v.size(), size_t(4));
	CHECK_EQ(v[0].chordSize, 1);
	CHECK_EQ(v[0].note, 60);
	CHECK_EQ(v[1].chordSize, 3); // Poly: the step's whole pool
	CHECK_EQ(int(v[1].chord[0]), 60);
	CHECK_EQ(int(v[1].chord[1]), 64);
	CHECK_EQ(int(v[1].chord[2]), 67);
	CHECK_EQ(v[2].chordSize, 1);
}

TEST_CASE(step_pool_takes_pitch_on_top) {
	Engine e;
	fourPulses(e);
	const int note[] = {62};
	e.pattern.locks[0].setPool(4, note, 1);
	e.pattern.locks[0].set(4, TP_PITCH, 12);
	CHECK_EQ(ons(e, -1e-9, 4.0 - 1e-6)[1].note, 74);
}

TEST_CASE(cycle_pool_lock_over_the_track) {
	Engine e;
	fourPulses(e);
	e.pattern.cycles[0].count = 2;
	const int a[] = {62}, b[] = {65};
	e.pattern.locks[0].setPool(4, a, 1);
	CHECK(e.pattern.cycles[0].cycles[1].locks.setPool(4, b, 1));
	CHECK(e.pattern.cycles[0].cycles[1].modified());
	std::vector<NoteEvent> v = ons(e, -1e-9, 8.0 - 1e-6);
	CHECK_EQ(v[1].note, 62); // cycle 1: the track's lock
	CHECK_EQ(v[5].note, 65); // cycle 2: its own
	e.pattern.cycles[0].cycles[1].locks.clearStep(4);
	CHECK(e.pattern.cycles[0].cycles[1].locks.empty());
}

TEST_CASE(clear_step_drops_the_pool) {
	StepLocks l;
	const int a[] = {62};
	l.setPool(9, a, 1);
	CHECK(l.poolAt(9));
	l.clearStep(9);
	CHECK(!l.poolAt(9));
	CHECK(!l.any(9));
	CHECK_EQ(l.pools, StepMask(0));
}

TEST_CASE(pool_locks_round_trip) {
	Pattern a;
	const int p[] = {48, 55, 63}, q[] = {70};
	a.locks[3].setPool(7, p, 3);
	a.cycles[3].count = 2;
	a.cycles[3].cycles[1].locks.setPool(2, q, 1);
	Pattern b;
	CHECK(patternFromJson(patternToJson(a), b));
	CHECK(a == b);
	CHECK(b.locks[3].poolAt(7) && b.locks[3].poolAt(7)->size == 3);
	CHECK(b.cycles[3].cycles[1].locks.poolAt(2) && b.cycles[3].cycles[1].locks.poolAt(2)->notes[0] == 70);
	CHECK(a.trackUsed(3));
	Pattern c;
	CHECK(!(c == a));
}
