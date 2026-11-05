#include "Test.hpp"
#include "core/Model.hpp"
#include "core/State.hpp"
#include "core/Tonal.hpp"

using namespace talea;

// The grid.

TEST_CASE(phrase_grid_round_trips_every_column) {
	PhraseGrid g = 0;
	for (int c = 0; c < kPhraseColumns; c++)
		g = setPhraseLevel(g, c, c % kPhraseLevels);
	for (int c = 0; c < kPhraseColumns; c++)
		CHECK_EQ(phraseLevel(g, c), c % kPhraseLevels);
}

TEST_CASE(phrase_grid_holds_the_top_column_at_the_top_level) {
	// Column 15 at level 11 sets bit 63: the case that can't be a signed 64-bit JSON int.
	PhraseGrid g = setPhraseLevel(0, kPhraseColumns - 1, kPhraseLevels - 1);
	CHECK_EQ(phraseLevel(g, kPhraseColumns - 1), kPhraseLevels - 1);
	CHECK(g >> 63 == 1);
}

TEST_CASE(phrase_grid_clamps_and_ignores_columns_outside_it) {
	PhraseGrid g = setPhraseLevel(0, 0, 99);
	CHECK_EQ(phraseLevel(g, 0), kPhraseLevels - 1);
	g = setPhraseLevel(g, 0, -5);
	CHECK_EQ(phraseLevel(g, 0), 0);
	CHECK_EQ(setPhraseLevel(g, kPhraseColumns, 3), g); // past the end: unchanged
	CHECK_EQ(setPhraseLevel(g, -1, 3), g);
	CHECK_EQ(phraseLevel(g, kPhraseColumns), 0);
	CHECK_EQ(phraseLevel(g, -1), 0);
}

TEST_CASE(phrase_grid_setting_one_column_leaves_the_others) {
	PhraseGrid g = kDefaultPhraseGrid;
	int before = phraseLevel(g, 5);
	g = setPhraseLevel(g, 4, 0);
	CHECK_EQ(phraseLevel(g, 4), 0);
	CHECK_EQ(phraseLevel(g, 5), before);
}

TEST_CASE(phrase_default_grid_is_a_ramp) {
	CHECK_EQ(phraseLevel(kDefaultPhraseGrid, 0), 0);
	CHECK_EQ(phraseLevel(kDefaultPhraseGrid, kPhraseColumns - 1), kPhraseLevels - 1);
	for (int c = 1; c < kPhraseColumns; c++)
		CHECK(phraseLevel(kDefaultPhraseGrid, c) >= phraseLevel(kDefaultPhraseGrid, c - 1));
}

// phraseValue.

TEST_CASE(phrase_user_reads_the_column_for_the_phase) {
	PhraseGrid g = 0;
	g = setPhraseLevel(g, 0, 0);
	g = setPhraseLevel(g, 1, kPhraseLevels - 1);
	// Column 0 covers phase 0 .. 1/16, column 1 the next sixteenth.
	CHECK_EQ(phraseValue(PHRASE_USER, 0.0, 0.0, g), 0.0);
	CHECK_EQ(phraseValue(PHRASE_USER, 1.0 / 16 + 0.001, 0.0, g), 1.0);
}

TEST_CASE(phrase_user_is_normalised_zero_to_one) {
	PhraseGrid flatLow = 0;
	PhraseGrid flatHigh = 0;
	for (int c = 0; c < kPhraseColumns; c++)
		flatHigh = setPhraseLevel(flatHigh, c, kPhraseLevels - 1);
	for (double p = 0.0; p < 1.0; p += 0.05) {
		CHECK_EQ(phraseValue(PHRASE_USER, p, 0.0, flatLow), 0.0);
		CHECK_EQ(phraseValue(PHRASE_USER, p, 0.0, flatHigh), 1.0);
	}
}

TEST_CASE(phrase_user_wraps_phase_like_the_others) {
	PhraseGrid g = setPhraseLevel(0, 0, kPhraseLevels - 1);
	CHECK_EQ(phraseValue(PHRASE_USER, 2.0, 0.0, g), phraseValue(PHRASE_USER, 0.0, 0.0, g));
}

TEST_CASE(phrase_cv_still_works_as_its_own_case) {
	// It used to be the switch's default; now PHRASE_USER sits past it.
	CHECK_EQ(phraseValue(PHRASE_CV, 0.0, -5.0, 0), 0.0);
	CHECK_EQ(phraseValue(PHRASE_CV, 0.0, 5.0, 0), 1.0);
	CHECK_EQ(phraseValue(PHRASE_CV, 0.0, 0.0, 0), 0.5);
}

TEST_CASE(phrase_existing_shapes_keep_their_indices) {
	// `phrase` is saved as an index, so User had to go on the end.
	CHECK_EQ(int(PHRASE_CADENCE_1), 0);
	CHECK_EQ(int(PHRASE_CV), 8);
	CHECK_EQ(int(PHRASE_USER), 9);
	CHECK_EQ(int(kNumPhrases), 10);
}

// How it reaches the notes.

TEST_CASE(phrase_user_shifts_notes_by_range) {
	Track t;
	t.reset();
	t.phrase = PHRASE_USER;
	t.set(TP_RANGE, 12.f);
	t.userPhrase = 0; // flat at the pool
	CHECK_EQ(t.noteFor(0, 0.0), kBaseNote);
	for (int c = 0; c < kPhraseColumns; c++)
		t.userPhrase = setPhraseLevel(t.userPhrase, c, kPhraseLevels - 1);
	CHECK_EQ(t.noteFor(0, 0.0), kBaseNote + 12); // full Range
}

TEST_CASE(phrase_user_result_is_snapped_to_the_scale) {
	Track t;
	t.reset();
	t.phrase = PHRASE_USER;
	t.set(TP_RANGE, 12.f);
	t.scale = 1; // the first named scale, not Chromatic
	for (int c = 0; c < kPhraseColumns; c++)
		t.userPhrase = setPhraseLevel(t.userPhrase, c, c % kPhraseLevels);
	for (int c = 0; c < kPhraseColumns; c++) {
		int note = t.noteFor(0, 4.0 * kGrooveBars[t.phraseLength] * (c + 0.5) / kPhraseColumns);
		CHECK(inScale(note, t.scaleMask(), t.root));
	}
}

// Scope: per track, per pattern, per cycle.

TEST_CASE(phrase_user_grid_round_trips_through_state) {
	Pattern a;
	a.tracks[3].phrase = PHRASE_USER;
	for (int c = 0; c < kPhraseColumns; c++)
		a.tracks[3].userPhrase = setPhraseLevel(a.tracks[3].userPhrase, c, (c * 7) % kPhraseLevels);
	Pattern b;
	CHECK(patternFromJson(patternToJson(a), b));
	CHECK(a == b);
	CHECK_EQ(b.tracks[3].userPhrase, a.tracks[3].userPhrase);
	CHECK_EQ(b.tracks[3].phrase, int(PHRASE_USER));
}

TEST_CASE(phrase_is_per_track) {
	Pattern a;
	a.tracks[0].phrase = PHRASE_USER;
	CHECK_EQ(a.tracks[1].phrase, int(PHRASE_CADENCE_1)); // its neighbour is untouched
}

TEST_CASE(phrase_is_per_cycle) {
	Pattern a;
	Track& base = a.tracks[2];
	TrackCycles& cyc = a.cycles[2];
	cyc.count = 2;
	base.phrase = PHRASE_SAW;

	Track& tn = cyc.tonalEdit(base, 1);
	tn.phrase = PHRASE_USER;
	tn.phraseLength = 0;
	tn.userPhrase = setPhraseLevel(0, 3, kPhraseLevels - 1);

	CHECK_EQ(cyc.resolve(base, 1).phrase, int(PHRASE_USER));
	CHECK_EQ(cyc.resolve(base, 1).phraseLength, 0);
	CHECK_EQ(cyc.resolve(base, 1).userPhrase, tn.userPhrase);
	CHECK_EQ(cyc.resolve(base, 0).phrase, int(PHRASE_SAW)); // cycle 0 still the track's
	CHECK_EQ(cyc.resolve(base, 0).userPhrase, base.userPhrase);
}

TEST_CASE(phrase_per_cycle_round_trips_through_state) {
	Pattern a;
	Track& base = a.tracks[2];
	TrackCycles& cyc = a.cycles[2];
	cyc.count = 2;
	Track& tn = cyc.tonalEdit(base, 1);
	tn.phrase = PHRASE_USER;
	tn.phraseLength = 2;
	tn.userPhrase = setPhraseLevel(kDefaultPhraseGrid, 15, kPhraseLevels - 1);

	Pattern b;
	CHECK(patternFromJson(patternToJson(a), b));
	CHECK(a == b);
	const Track& got = b.cycles[2].cycles[1].tonal;
	CHECK_EQ(got.phrase, int(PHRASE_USER));
	CHECK_EQ(got.phraseLength, 2);
	CHECK_EQ(got.userPhrase, tn.userPhrase);
}

TEST_CASE(phrase_a_differing_grid_makes_the_track_used) {
	Pattern a;
	CHECK(!a.trackUsed(4));
	a.tracks[4].userPhrase = setPhraseLevel(a.tracks[4].userPhrase, 0, kPhraseLevels - 1);
	CHECK(a.trackUsed(4));
}
