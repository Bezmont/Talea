#include "Test.hpp"
#include "core/Engine.hpp"
#include "core/State.hpp"

#include <cmath>
#include <vector>

using namespace talea;

// Phrase / Groove SYNC = LOOP: their length counts loops of the track instead of bars, so a
// loop that isn't a whole number of bars (14 steps of 1/16 = 3.5 quarters) restarts them with
// step 1 instead of drifting against them.

namespace {

/** Note-ons of `track` up to just before `to` (a window includes its end). */
std::vector<NoteEvent> trackOns(Engine& e, int track, double to) {
	to -= 1e-6;
	std::vector<NoteEvent> out;
	EventList list;
	for (double t = -1e-9; t < to - 1e-12; t += 0.5) {
		list.clear();
		e.process(t, std::min(t + 0.5, to), list);
		for (const NoteEvent& n : list) {
			if (n.on && n.track == track)
				out.push_back(n);
		}
	}
	return out;
}

bool near(double a, double b) {
	return std::fabs(a - b) < 1e-6;
}

/** Track 0: 14 steps of 1/16, every step a pulse, a Saw phrase over a 12-semitone Range. */
void sawOn14Steps(Engine& e, bool loopSync, int length) {
	Track& t = e.pattern.tracks[0];
	t.set(TP_STEPS, 14);
	t.set(TP_PULSES, 14);
	t.set(TP_RANGE, 12);
	t.phrase = PHRASE_SAW;
	t.phraseLength = length;
	t.phraseLoop = loopSync;
}

} // namespace

TEST_CASE(phrase_loop_sync_restarts_with_step_1) {
	Engine e;
	sawOn14Steps(e, true, 0); // 1 loop
	std::vector<NoteEvent> on = trackOns(e, 0, 4 * 3.5);
	CHECK_EQ(int(on.size()), 4 * 14);
	for (int loop = 0; loop < 4 && int(on.size()) == 4 * 14; loop++) {
		CHECK_EQ(on[loop * 14].note, kBaseNote);          // every loop starts at the pool
		CHECK_EQ(on[loop * 14 + 7].note, kBaseNote + 6);  // halfway up the saw
		CHECK_EQ(on[loop * 14 + 13].note, on[13].note);   // and every loop ends the same
	}
}

TEST_CASE(phrase_bar_sync_still_drifts_against_an_odd_loop) {
	// The default: the phrase runs on bars, so the second 3.5-quarter loop starts late in it.
	Engine e;
	sawOn14Steps(e, false, 0); // 1 bar
	std::vector<NoteEvent> on = trackOns(e, 0, 2 * 3.5);
	CHECK_EQ(int(on.size()), 2 * 14);
	if (int(on.size()) == 2 * 14) {
		CHECK_EQ(on[0].note, kBaseNote);
		CHECK(on[14].note != kBaseNote);
	}
}

TEST_CASE(phrase_loop_sync_length_counts_loops) {
	Engine e;
	sawOn14Steps(e, true, 1); // 2 loops
	std::vector<NoteEvent> on = trackOns(e, 0, 4 * 3.5);
	CHECK_EQ(int(on.size()), 4 * 14);
	if (int(on.size()) == 4 * 14) {
		CHECK_EQ(on[0].note, kBaseNote);
		CHECK_EQ(on[14].note, kBaseNote + 6); // second loop: halfway through the phrase
		CHECK_EQ(on[28].note, kBaseNote);     // third: the phrase starts again
	}
}

TEST_CASE(phrase_loop_sync_follows_each_cycle_loop) {
	// Cycles of 14 and 10 steps: each loop, whatever its length, starts the phrase again.
	Engine e;
	sawOn14Steps(e, true, 0);
	TrackCycles& cyc = e.pattern.cycles[0];
	cyc.count = 2;
	cyc.set(1, TP_STEPS, 10);
	cyc.set(1, TP_PULSES, 10);
	double starts[] = {0.0, 3.5, 6.0, 9.5, 12.0};
	std::vector<NoteEvent> on = trackOns(e, 0, 12.5);
	int found = 0;
	for (const NoteEvent& n : on) {
		for (double s : starts) {
			if (near(n.time, s)) {
				CHECK_EQ(n.note, kBaseNote);
				found++;
			}
		}
	}
	CHECK_EQ(found, 5);
}

TEST_CASE(phrase_loop_sync_without_the_engine_uses_the_track_loop) {
	Track t;
	t.set(TP_STEPS, 14);
	t.set(TP_RANGE, 12);
	t.phrase = PHRASE_SAW;
	t.phraseLength = 0;
	t.phraseLoop = true;
	CHECK_EQ(t.noteFor(0, 3.5), kBaseNote);          // a loop in, back at the start
	CHECK_EQ(t.noteFor(0, 1.75), kBaseNote + 6);     // half a loop
	CHECK_EQ(t.noteFor(0, 99.0, 0.0, 0, 2.5), kBaseNote + 6); // an explicit position wins
}

TEST_CASE(groove_loop_sync_restarts_percussion_with_step_1) {
	auto build = [](Engine& e, bool sync) {
		Track& t = e.pattern.tracks[0];
		t.set(TP_STEPS, 14);
		t.set(TP_PULSES, 14);
		t.set(TP_VELOCITY, 64);
		t.set(TP_ACCENT, 30);
		t.groove = GROOVE_AGOGO;
		t.grooveLoop = sync;
	};
	Engine synced, bars;
	build(synced, true);
	build(bars, false);
	std::vector<NoteEvent> a = trackOns(synced, 0, 3 * 3.5), b = trackOns(bars, 0, 3 * 3.5);
	CHECK_EQ(int(a.size()), 3 * 14);
	CHECK_EQ(int(b.size()), 3 * 14);
	if (int(a.size()) == 3 * 14 && int(b.size()) == 3 * 14) {
		for (int i = 0; i < 14; i++) {
			CHECK_EQ(a[14 + i].velocity, a[i].velocity); // every loop accents alike
			CHECK_EQ(a[28 + i].velocity, a[i].velocity);
		}
		CHECK(b[14].velocity != b[0].velocity); // on bars, loop 2 starts mid-bar
	}
}

TEST_CASE(groove_loop_sync_waveform_spans_loops) {
	Track t;
	t.set(TP_STEPS, 14);
	t.set(TP_ACCENT, 100);
	t.groove = GROOVE_PULSE;
	t.grooveLength = 0; // 1 loop
	t.grooveLoop = true;
	CHECK_EQ(t.accented(64, 0.0), 127);       // first half of the loop: up
	CHECK_EQ(t.accented(64, 2.0), 1);         // second half (past 1.75): down
	CHECK_EQ(t.accented(64, 3.5), 127);       // the next loop starts up again
	t.grooveLength = 1;                       // 2 loops
	CHECK_EQ(t.accented(64, 3.6), 1);         // second loop = second half
}

TEST_CASE(engine_loops_at_counts_loops) {
	Engine e;
	Track& t = e.pattern.tracks[0];
	t.set(TP_STEPS, 14);
	CHECK(near(e.loopsAt(0, 0.0), 0.0));
	CHECK(near(e.loopsAt(0, 1.75), 0.5));
	CHECK(near(e.loopsAt(0, 7.0), 2.0));
	CHECK(near(e.loopsAt(0, -3.0), 0.0)); // before the start
	t.set(TP_LENGTH, 6.f);                 // Length 1 bar: the loop is 4 quarters
	CHECK(near(e.loopsAt(0, 6.0), 1.5));
}

TEST_CASE(loop_sync_round_trips_through_state) {
	Pattern a;
	a.tracks[1].grooveLoop = true;
	a.tracks[2].phraseLoop = true;
	TrackCycles& cyc = a.cycles[3];
	cyc.count = 2;
	cyc.tonalEdit(a.tracks[3], 1).phraseLoop = true;
	Pattern b;
	CHECK(patternFromJson(patternToJson(a), b));
	CHECK(a == b);
	CHECK(b.tracks[1].grooveLoop);
	CHECK(!b.tracks[1].phraseLoop);
	CHECK(b.tracks[2].phraseLoop);
	CHECK(b.cycles[3].cycles[1].tonal.phraseLoop);
	CHECK(!b.cycles[3].resolve(b.tracks[3], 0).phraseLoop);
	CHECK(b.cycles[3].resolve(b.tracks[3], 1).phraseLoop);
}

TEST_CASE(loop_sync_defaults_off_and_counts_as_a_change) {
	Pattern a;
	CHECK(!a.tracks[0].phraseLoop);
	CHECK(!a.tracks[0].grooveLoop);
	Pattern b = a;
	b.tracks[0].phraseLoop = true;
	CHECK(a != b);
	CHECK(b.trackUsed(0));
}
