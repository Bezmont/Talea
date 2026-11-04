#include "Test.hpp"
#include "core/Engine.hpp"
#include "core/State.hpp"

#include <cmath>
#include <vector>

using namespace talea;

namespace {

/** Note-on times of `track` in (from, to], in windows of `window`. */
std::vector<double> onTimes(Engine& e, double from, double to, int track = 0, double window = 0.05) {
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

bool near(double a, double b) {
	return std::fabs(a - b) < 1e-5;
}

/** Track 0 plays `n` pulses of 16 steps (a one-bar loop). */
Pattern pulses(int n) {
	Pattern p;
	p.tracks[0].set(TP_PULSES, n);
	return p;
}

void startAtZero(Engine& e) {
	EventList out;
	e.setSongOn(true, -1e-9, out);
}

} // namespace

TEST_CASE(song_plays_each_entry_for_its_bars_then_ends) {
	Engine e;
	e.storePattern(0, pulses(4));
	e.storePattern(1, pulses(2));
	e.song.insert(0, {0, 2});
	e.song.insert(1, {1, 1});
	startAtZero(e);
	CHECK_EQ(e.currentPattern(), 0);
	CHECK_EQ(e.songEntry(), 0);
	// Entry 1: quarter notes for two bars; entry 2: half notes for one bar; then it ends (the
	// host stops).
	std::vector<double> v = onTimes(e, -1e-9, 11.99);
	CHECK_EQ(v.size(), size_t(8 + 2));
	CHECK(near(v[7], 7.0) && near(v[8], 8.0) && near(v[9], 10.0));
	CHECK_EQ(e.currentPattern(), 1);
	CHECK(!e.takeSongEnd());
	EventList list;
	e.process(11.99, 12.05, list); // the window the song ends in: nothing from the bar line on
	for (const NoteEvent& n : list)
		CHECK(!n.on);
	CHECK(e.takeSongEnd());
	CHECK(!e.takeSongEnd());
	CHECK_EQ(e.songEntry(), -1);
}

TEST_CASE(song_restarts_the_pattern_within_an_entry_and_loops) {
	Engine e;
	// A 1.5-bar pattern: track 0 loops over 24 sixteenths, track 1 over one bar.
	Pattern p;
	p.tracks[0].set(TP_STEPS, 24);
	p.tracks[0].set(TP_PULSES, 3);
	p.tracks[1].set(TP_PULSES, 1);
	e.storePattern(0, p);
	CHECK_EQ(e.defaultBars(0), 2);
	e.song.insert(0, {0, e.defaultBars(0)});
	e.song.loop = true;
	startAtZero(e);
	// Track 1 restarts with the pattern at 6 and with the song's loop at 8.
	std::vector<double> v = onTimes(e, -1e-9, 8.5, 1);
	CHECK_EQ(v.size(), size_t(4));
	CHECK(near(v[0], 0.0) && near(v[1], 4.0) && near(v[2], 6.0) && near(v[3], 8.0));
	CHECK(!e.takeSongEnd());
	CHECK_EQ(e.songEntry(), 0);
	CHECK(e.patternRestarts() == 1u);
}

TEST_CASE(song_override_and_resume_at_the_bar_it_left) {
	Engine e;
	e.storePattern(0, pulses(4));
	e.storePattern(1, pulses(2));
	e.storePattern(2, pulses(1));
	e.song.insert(0, {0, 4});
	e.song.insert(1, {1, 4});
	startAtZero(e);
	onTimes(e, -1e-9, 5.0);
	EventList out;
	e.queuePattern(2, 5.0, out, true); // a click: switches at once
	CHECK_EQ(e.currentPattern(), 2);
	CHECK(e.songOverridden());
	CHECK_EQ(e.songEntry(), -1);
	CHECK_EQ(e.songResumeEntry(), 0);
	CHECK_EQ(e.songResumeBar(), 1);
	onTimes(e, 5.0, 9.5);
	CHECK_EQ(e.currentPattern(), 2);
	e.resumeSong(9.5);
	CHECK(e.songPending());
	CHECK(near(e.songPendingTime(), 12.0));
	onTimes(e, 9.5, 12.5);
	CHECK_EQ(e.currentPattern(), 0);
	CHECK_EQ(e.songEntry(), 0);
	CHECK(!e.songOverridden());
	// Back at bar 2 of the entry: three bars left, then entry 2 starts at 24.
	CHECK(near(e.songEntryStart(), 8.0));
	onTimes(e, 12.5, 23.5);
	CHECK_EQ(e.currentPattern(), 0);
	onTimes(e, 23.5, 24.5);
	CHECK_EQ(e.currentPattern(), 1);
	CHECK_EQ(e.songEntry(), 1);
}

TEST_CASE(song_queued_switch_overrides_when_it_lands) {
	Engine e;
	e.storePattern(0, pulses(4));
	e.storePattern(2, pulses(1));
	e.song.insert(0, {0, 8});
	startAtZero(e);
	onTimes(e, -1e-9, 1.0);
	EventList out;
	e.queuePattern(2, 1.0, out); // End of pattern: lands at 4
	CHECK(!e.songOverridden());
	onTimes(e, 1.0, 4.5);
	CHECK_EQ(e.currentPattern(), 2);
	CHECK(e.songOverridden());
	CHECK_EQ(e.songResumeBar(), 1);
}

TEST_CASE(song_edits_keep_the_playing_entry) {
	Engine e;
	e.storePattern(1, pulses(2));
	e.song.insert(0, {0, 1});
	e.song.insert(1, {1, 4});
	startAtZero(e);
	onTimes(e, -1e-9, 5.0);
	CHECK_EQ(e.songEntry(), 1);
	e.songInsert(0, {3, 2}, 5.0);
	CHECK_EQ(e.songEntry(), 2);
	e.songRemove(0, 5.0);
	CHECK_EQ(e.songEntry(), 1);
	// Removing the playing entry: the next (none: the end) comes on the next bar line.
	e.songRemove(1, 5.0);
	CHECK(e.songPending());
	CHECK(near(e.songPendingTime(), 8.0));
	onTimes(e, 5.0, 8.5);
	CHECK(e.takeSongEnd());
}

TEST_CASE(song_off_leaves_the_pattern_playing) {
	Engine e;
	e.storePattern(1, pulses(2));
	e.song.insert(0, {1, 1});
	startAtZero(e);
	CHECK_EQ(e.currentPattern(), 1);
	EventList out;
	e.setSongOn(false, 2.0, out);
	onTimes(e, -1e-9, 12.0);
	CHECK_EQ(e.currentPattern(), 1);
	CHECK(!e.takeSongEnd());
}

TEST_CASE(song_round_trips) {
	Song a;
	a.insert(0, {3, 8});
	a.insert(1, {15, 999});
	a.loop = true;
	Song b;
	CHECK(songFromJson(songToJson(a), b));
	CHECK(a == b);
	CHECK_EQ(b.totalBars(), 1007);
	CHECK(songFromJson("{\"entries\":[{\"pattern\":20,\"bars\":4},{\"pattern\":2,\"bars\":0},{\"pattern\":1,\"bars\":5000}]}", b));
	CHECK_EQ(b.count, 1);
	CHECK_EQ(b.entries[0].bars, kMaxSongBars);
}
