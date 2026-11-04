#include "Test.hpp"
#include "core/Engine.hpp"
#include "core/State.hpp"

#include <cmath>
#include <vector>

using namespace talea;

namespace {

template <size_t N>
float indexOf(const NamedValue (&list)[N], const char* name) {
	for (size_t i = 0; i < N; i++) {
		if (std::string(list[i].name) == name)
			return float(i);
	}
	return -1.f;
}

/** Note-on times of `track` in (from, to], in windows of `window`. */
std::vector<double> onTimes(Engine& e, double from, double to, double window = 0.05, int track = 0) {
	std::vector<double> out;
	EventList list;
	for (int i = 0;; i++) {
		double a = from + i * window, b = from + (i + 1) * window;
		if (a >= to - 1e-9)
			break;
		list.clear();
		e.process(a, b >= to - 1e-9 ? to : b, list);
		for (const NoteEvent& n : list) {
			if (n.on && n.track == track)
				out.push_back(n.time);
		}
	}
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

/** Track 0 plays a note on every beat. */
void everyBeat(Engine& e) {
	e.pattern.tracks[0].set(TP_PULSES, 4);
}

} // namespace

TEST_CASE(muted_track_plays_nothing) {
	Engine e;
	everyBeat(e);
	EventList out;
	e.setMuted(0, true, 0.0, out);
	CHECK(onTimes(e, -1e-9, 4.0).empty());
	e.setMuted(0, false, 4.0, out);
	CHECK(sameTimes(onTimes(e, 4.0, 6.0), {5.0, 6.0}));
}

TEST_CASE(staged_mutes_wait_for_commit) {
	Engine e;
	everyBeat(e);
	e.stageMute(0);
	e.stageMute(3);
	e.stageMute(3); // staged twice = not staged
	CHECK_EQ(e.stagedMutes(), uint16_t(1));
	CHECK(sameTimes(onTimes(e, -1e-9, 2.0), {0.0, 1.0, 2.0}));
	e.clearStagedMutes();
	e.commitMutes(2.0); // nothing staged: nothing happens
	CHECK(sameTimes(onTimes(e, 2.0, 3.0), {3.0}));
}

TEST_CASE(commit_now_applies_at_the_next_window) {
	Engine e;
	everyBeat(e);
	onTimes(e, -1e-9, 1.5);
	e.stageMute(0);
	e.commitMutes(1.5);
	CHECK(onTimes(e, 1.5, 8.0).empty());
	CHECK(e.pattern.tracks[0].muted);
	CHECK_EQ(e.scheduledMutes(), uint16_t(0));
}

TEST_CASE(commit_on_bar_lands_on_the_downbeat) {
	// Same result for any window size; the pulse on the boundary already follows the mute.
	for (double window : {0.05, 0.3, 1.0, 3.0}) {
		Engine e;
		everyBeat(e);
		e.muteTiming = MUTE_BAR;
		onTimes(e, -1e-9, 1.5, window);
		e.stageMute(0);
		e.commitMutes(1.5);
		CHECK_EQ(e.scheduledMuteTime(), 4.0);
		CHECK_EQ(e.stagedMutes(), uint16_t(0));
		CHECK(sameTimes(onTimes(e, 1.5, 8.0, window), {2.0, 3.0}));
		CHECK(e.pattern.tracks[0].muted);
		// ... and unmuting on the next bar brings the downbeat back.
		e.stageMute(0);
		e.commitMutes(8.0);
		CHECK_EQ(e.scheduledMuteTime(), 12.0);
		CHECK(sameTimes(onTimes(e, 8.0, 13.0, window), {12.0, 13.0}));
	}
}

TEST_CASE(commit_timing_boundaries) {
	Engine e;
	e.stageMute(0);
	e.muteTiming = MUTE_BEAT;
	e.commitMutes(2.0); // on a beat: the next one, the current one has played
	CHECK_EQ(e.scheduledMuteTime(), 3.0);
	e.stageMute(1);
	e.muteTiming = MUTE_PATTERN;
	e.pattern.tracks[5].set(TP_LENGTH, indexOf(kLengths, "3 bars")); // longest Length: 12
	e.commitMutes(2.0);
	CHECK_EQ(e.scheduledMuteTime(), 12.0);
	CHECK_EQ(e.scheduledMutes(), uint16_t(3)); // the earlier change moved with it
	e.clearMuteQueue();
	CHECK_EQ(e.scheduledMutes(), uint16_t(0));
}

TEST_CASE(mute_lets_repeats_finish_or_cuts_them) {
	for (bool cut : {false, true}) {
		Engine e;
		Track& t = e.pattern.tracks[0];
		t.set(TP_PULSES, 1);
		t.set(TP_REPEATS, 3);
		t.set(TP_REPEAT_TIME, indexOf(kRepeatTimes, "1/4"));
		e.muteCuts = cut;
		CHECK(sameTimes(onTimes(e, -1e-9, 0.5), {0.0}));
		e.stageMute(0);
		e.commitMutes(0.5);
		EventList list;
		e.process(0.5, 0.55, list);
		bool cutOff = false;
		for (const NoteEvent& n : list)
			cutOff |= !n.on && n.track == 0 && std::fabs(n.time - 0.5) < 1e-9;
		CHECK_EQ(cutOff, cut);
		std::vector<double> later = onTimes(e, 0.55, 8.0);
		if (cut)
			CHECK(later.empty());
		else
			CHECK(sameTimes(later, {1.0, 2.0, 3.0})); // the burst finishes; the next pulse is muted
	}
}

TEST_CASE(mute_state_round_trip) {
	Pattern a;
	a.tracks[7].muted = true;
	Pattern b;
	CHECK(patternFromJson(patternToJson(a), b));
	CHECK(b.tracks[7].muted && !b.tracks[6].muted);
}
