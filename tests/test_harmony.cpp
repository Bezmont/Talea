#include "Test.hpp"
#include "core/Model.hpp"

#include <vector>

using namespace talea;

namespace {

Track triad() {
	Track t;
	t.reset();
	t.togglePoolNote(64);
	t.togglePoolNote(67);
	return t; // C4 E4 G4, chromatic
}

std::vector<int> pool(const Track& t) {
	return std::vector<int>(t.pool, t.pool + t.poolSize);
}

} // namespace

// Harmony itself: one-way by design, so turning back lands somewhere else.

TEST_CASE(harmony_cw_moves_the_lowest_note_up) {
	Track t = triad();
	t.harmony(1);
	CHECK(pool(t) == std::vector<int>({61, 64, 67}));
}

TEST_CASE(harmony_ccw_moves_the_highest_note_down) {
	Track t = triad();
	t.harmony(-1);
	CHECK(pool(t) == std::vector<int>({60, 64, 66}));
}

TEST_CASE(harmony_skips_notes_already_in_the_pool) {
	Track t;
	t.reset();
	t.togglePoolNote(61); // C4 C#4
	t.harmony(1);         // lowest C4 steps up past C#4
	CHECK(pool(t) == std::vector<int>({61, 62}));
}

TEST_CASE(harmony_does_not_round_trip) {
	Track t = triad();
	t.harmony(1);
	t.harmony(-1);
	CHECK(pool(t) != std::vector<int>({60, 64, 67}));
}

TEST_CASE(harmony_round_trips_for_a_single_note_pool) {
	Track t;
	t.reset(); // C4 alone: lowest and highest are the same note
	t.harmony(1);
	CHECK_EQ(t.pool[0], 61);
	t.harmony(-1);
	CHECK_EQ(t.pool[0], 60);
}

TEST_CASE(harmony_alternating_contracts_the_pool) {
	// Both directions squeeze inwards: CW raises the floor, CCW lowers the ceiling.
	Track t = triad();
	for (int i = 0; i < 3; i++)
		t.harmony(1);
	for (int i = 0; i < 3; i++)
		t.harmony(-1);
	CHECK(pool(t) == std::vector<int>({62, 63, 64})); // a cluster, from a triad
}

TEST_CASE(harmony_stops_at_the_midi_range) {
	Track t;
	t.reset();
	t.pool[0] = 127;
	for (int i = 0; i < 4; i++)
		t.harmony(1);
	CHECK_EQ(t.pool[0], 127);
}

// HarmonyUndo: the way back.

TEST_CASE(undo_restores_the_pool_a_run_started_from) {
	Track t = triad();
	HarmonyUndo u;
	for (int i = 0; i < 5; i++) {
		u.before(0, -1, t);
		t.harmony(1);
		u.after(0, t);
	}
	CHECK(pool(t) != std::vector<int>({60, 64, 67}));
	CHECK(u.restore(0, -1, t));
	CHECK(pool(t) == std::vector<int>({60, 64, 67}));
}

TEST_CASE(undo_restores_after_mixed_directions) {
	Track t = triad();
	HarmonyUndo u;
	for (int d : {1, 1, -1, 1, -1, -1}) {
		u.before(0, -1, t);
		t.harmony(d);
		u.after(0, t);
	}
	CHECK(u.restore(0, -1, t));
	CHECK(pool(t) == std::vector<int>({60, 64, 67}));
}

TEST_CASE(undo_has_nothing_before_any_harmony) {
	Track t = triad();
	HarmonyUndo u;
	CHECK(!u.has(0, -1));
	CHECK(!u.restore(0, -1, t));
	CHECK(pool(t) == std::vector<int>({60, 64, 67})); // and left the pool alone
}

TEST_CASE(undo_is_spent_once_restored) {
	Track t = triad();
	HarmonyUndo u;
	u.before(0, -1, t);
	t.harmony(1);
	u.after(0, t);
	CHECK(u.restore(0, -1, t));
	CHECK(!u.has(0, -1));
	CHECK(!u.restore(0, -1, t));
}

TEST_CASE(undo_starts_a_new_run_when_the_pool_is_edited_elsewhere) {
	// A KEYS-page edit between runs: restore goes back to the edited pool, not past it.
	Track t = triad();
	HarmonyUndo u;
	u.before(0, -1, t);
	t.harmony(1);
	u.after(0, t); // C#4 E4 G4

	t.togglePoolNote(72); // + C5, not via Harmony

	u.before(0, -1, t);
	t.harmony(1);
	u.after(0, t);
	CHECK(u.restore(0, -1, t));
	CHECK(pool(t) == std::vector<int>({61, 64, 67, 72}));
}

TEST_CASE(undo_starts_a_new_run_when_the_slot_changes) {
	Track t = triad();
	HarmonyUndo u;
	u.before(0, -1, t); // the track's own pool
	t.harmony(1);
	u.after(0, t);

	u.before(0, 2, t); // now cycle 2's pool
	t.harmony(1);
	u.after(0, t);

	CHECK(!u.has(0, -1)); // the track-level run is gone
	CHECK(u.has(0, 2));
	CHECK(u.restore(0, 2, t));
	CHECK(pool(t) == std::vector<int>({61, 64, 67}));
}

TEST_CASE(undo_keeps_tracks_apart) {
	Track a = triad();
	Track b;
	b.reset();
	HarmonyUndo u;
	u.before(0, -1, a);
	a.harmony(1);
	u.after(0, a);
	u.before(1, -1, b);
	b.harmony(1);
	u.after(1, b);

	CHECK(u.restore(0, -1, a));
	CHECK(pool(a) == std::vector<int>({60, 64, 67}));
	CHECK(u.has(1, -1)); // track 1's run is untouched
	CHECK(u.restore(1, -1, b));
	CHECK(pool(b) == std::vector<int>({60}));
}

TEST_CASE(undo_ignores_out_of_range_tracks) {
	Track t = triad();
	HarmonyUndo u;
	u.before(-1, -1, t);
	u.before(kNumTracks, -1, t);
	u.after(-1, t);
	CHECK(!u.has(-1, -1));
	CHECK(!u.has(kNumTracks, -1));
	CHECK(!u.restore(-1, -1, t));
}
