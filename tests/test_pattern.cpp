#include "Test.hpp"
#include "core/Engine.hpp"

#include <cmath>
#include <vector>

using namespace talea;

namespace {

/** Note-on times of track 0 in (from, to], in windows of `window`. */
std::vector<double> onTimes(Engine& e, double from, double to, double window = 0.05) {
	std::vector<double> out;
	EventList list;
	for (int i = 0;; i++) {
		double a = from + i * window, b = from + (i + 1) * window;
		if (a >= to - 1e-9)
			break;
		list.clear();
		e.process(a, b >= to - 1e-9 ? to : b, list);
		for (const NoteEvent& n : list) {
			if (n.on && n.track == 0)
				out.push_back(n.time);
		}
	}
	return out;
}

bool near(double a, double b) {
	return std::fabs(a - b) < 1e-5;
}

/** A pattern whose track 0 plays `pulses` of 16 steps (one 4-quarter loop). */
Pattern pulses(int n) {
	Pattern p;
	p.tracks[0].set(TP_PULSES, n);
	return p;
}

} // namespace

TEST_CASE(pattern_switches_at_pattern_end_and_restarts) {
	Engine e;
	e.pattern = pulses(16);
	e.storePattern(1, pulses(4));
	onTimes(e, -1e-9, 2.0);
	EventList out;
	e.queuePattern(1, 2.0, out);
	CHECK_EQ(e.queuedPattern(), 1);
	CHECK(near(e.patternSwitchTime(), 4.0));
	std::vector<double> notes = onTimes(e, 2.0, 8.0);
	CHECK_EQ(e.currentPattern(), 1);
	CHECK_EQ(e.queuedPattern(), -1);
	// 1/16 notes up to 3.75, then quarter notes from the switch.
	CHECK_EQ(notes.size(), size_t(7 + 5));
	CHECK(near(notes[6], 3.75));
	CHECK(near(notes[7], 4.0) && near(notes[11], 8.0));
	CHECK_EQ(e.patternSlot(0).tracks[0].pulses(), 16);
}

TEST_CASE(pattern_immediate_switch_restarts_from_step_1) {
	Engine e;
	e.storePattern(2, pulses(4));
	onTimes(e, -1e-9, 1.3);
	EventList out;
	e.queuePattern(2, 1.3, out, true);
	CHECK_EQ(e.currentPattern(), 2);
	std::vector<double> notes = onTimes(e, 1.3, 3.4);
	CHECK_EQ(notes.size(), size_t(3));
	CHECK(near(notes[0], 1.3) && near(notes[1], 2.3) && near(notes[2], 3.3));
	CHECK_EQ(e.stepAt(0, 1.3 + 0.5 + 1e-4), 2);
	CHECK(near(e.patternOrigin(), 1.3));
	// Bars count from the pattern's start.
	e.muteTiming = MUTE_BAR;
	e.stageMute(0);
	e.commitMutes(3.4);
	CHECK(near(e.scheduledMuteTime(), 5.3));
	// Reset: the pattern starts over at 0.
	e.rewind();
	CHECK_EQ(e.stepAt(0, 0.5), 2);
}

TEST_CASE(pattern_edits_stay_in_slot_and_temp_is_dropped) {
	Engine e;
	e.pattern.tracks[0].set(TP_PULSES, 7);
	e.beginTemp();
	e.pattern.tracks[0].set(TP_PULSES, 3);
	EventList out;
	e.queuePattern(1, 0.0, out, true);
	CHECK(!e.tempActive());
	CHECK_EQ(e.patternSlot(0).tracks[0].pulses(), 7);
	CHECK(e.patternUsed(0));
	CHECK(!e.patternUsed(1));
	e.pattern.tracks[5].set(TP_STEPS, 5);
	e.queuePattern(0, 1.0, out, true);
	CHECK_EQ(e.pattern.tracks[0].pulses(), 7);
	CHECK_EQ(e.patternSlot(1).tracks[5].steps(), 5);
	CHECK(e.patternUsed(1));
	e.storePattern(1, Pattern());
	CHECK(!e.patternUsed(1));
	// Queueing the playing pattern cancels a queued switch.
	e.queuePattern(3, 1.0, out);
	e.queuePattern(0, 1.0, out);
	CHECK_EQ(e.queuedPattern(), -1);
}

TEST_CASE(pattern_chain_moves_on_at_each_pattern_end) {
	Engine e;
	Pattern a = pulses(4), b = pulses(2);
	for (Track& t : b.tracks)
		t.set(TP_STEPS, 8); // 2 quarters long
	e.storePattern(1, a);
	e.storePattern(2, b);
	EventList out;
	e.chainPattern(1, -1e-9, out, true);
	e.chainPattern(2, -1e-9, out);
	CHECK_EQ(e.currentPattern(), 1);
	CHECK_EQ(e.queuedPattern(), 2);
	CHECK_EQ(e.chainLength(), 2);
	// 1 plays [0, 4), 2 [4, 6), 1 [6, 10), 2 [10, 12).
	std::vector<int> playing;
	for (double t = 0.5; t < 12.0; t += 1.0) {
		onTimes(e, t - 0.5 - (t < 1 ? 1e-9 : 0), t);
		playing.push_back(e.currentPattern());
	}
	std::vector<int> expected = {1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 2, 2};
	CHECK(playing == expected);
	CHECK_EQ(e.chainPosition(), 1);
	// A plain queue ends the chain.
	e.queuePattern(5, 12.0, out);
	CHECK_EQ(e.chainLength(), 0);
	CHECK_EQ(e.queuedPattern(), 5);
}

TEST_CASE(pattern_chain_appends_after_the_playing_entry) {
	Engine e;
	EventList out;
	e.chainPattern(0, 0.0, out); // the playing pattern: the chain starts now
	CHECK_EQ(e.chainPosition(), 0);
	CHECK_EQ(e.queuedPattern(), -1);
	e.chainPattern(3, 0.0, out);
	CHECK_EQ(e.queuedPattern(), 3);
	CHECK(near(e.patternSwitchTime(), 4.0));
	onTimes(e, -1e-9, 4.5);
	CHECK_EQ(e.currentPattern(), 3);
	CHECK_EQ(e.queuedPattern(), 0); // wraps round
	e.chainPattern(4, 4.5, out);     // playing the last entry: 4 comes next instead
	CHECK_EQ(e.queuedPattern(), 4);
	e.clearChain();
	CHECK_EQ(e.queuedPattern(), -1);
}

TEST_CASE(pattern_rewind_keeps_queue_and_reset_bank_clears) {
	Engine e;
	EventList out;
	onTimes(e, -1e-9, 6.0);
	e.queuePattern(1, 6.0, out);
	CHECK(near(e.patternSwitchTime(), 8.0));
	e.rewind();
	CHECK(near(e.patternSwitchTime(), 4.0));
	e.storePattern(7, pulses(3));
	e.resetBank();
	CHECK(!e.patternUsed(7));
	CHECK_EQ(e.queuedPattern(), -1);
	CHECK_EQ(e.currentPattern(), 0);
	Pattern p = pulses(9);
	e.storePattern(4, p);
	e.setCurrentPattern(4);
	CHECK_EQ(e.pattern.tracks[0].pulses(), 9);
	CHECK(e.patternSlot(4) == p);
	CHECK(e.patternSlot(0) == Pattern());
}
