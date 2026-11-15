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

// ---- the song on the host's timeline (Live) ----

namespace {

/** Entries {slot 0, 2 bars}, {slot 1, 1 bar}, {slot 2, 1 bar}: patterns of 4, 2 and 1 pulses. */
void timelineSong(Engine& e) {
	e.storePattern(0, pulses(4));
	e.storePattern(1, pulses(2));
	e.storePattern(2, pulses(1));
	e.song.insert(0, {0, 2});
	e.song.insert(1, {1, 1});
	e.song.insert(2, {2, 1});
	e.songTimeline = true;
}

} // namespace

TEST_CASE(song_timeline_plays_what_the_song_has_there) {
	Engine e;
	timelineSong(e);
	startAtZero(e);
	CHECK_EQ(e.songEntry(), 0);
	// Play from bar 3 (time 8): entry 2, as if the song had played from the start.
	e.jump(8.0 - 1e-9);
	CHECK_EQ(e.songEntry(), 1);
	CHECK_EQ(e.currentPattern(), 1);
	CHECK(near(e.songEntryStart(), 8.0));
	std::vector<double> v = onTimes(e, 8.0 - 1e-9, 12.0 - 1e-6);
	CHECK_EQ(v.size(), size_t(2));
	CHECK(near(v[0], 8.0) && near(v[1], 10.0));
	// Mid-bar, back into entry 1's second bar: the pattern where it would be.
	e.jump(5.0 - 1e-9);
	CHECK_EQ(e.songEntry(), 0);
	CHECK_EQ(e.currentPattern(), 0);
	CHECK(near(e.songEntryStart(), 0.0));
	v = onTimes(e, 5.0 - 1e-9, 8.0 - 1e-6);
	CHECK_EQ(v.size(), size_t(3));
	CHECK(near(v[0], 5.0));
	// Past the end (bar 5 on), no Loop: silent, and the host isn't told to stop.
	e.jump(16.0 - 1e-9);
	CHECK(e.songOver());
	CHECK(onTimes(e, 16.0 - 1e-9, 24.0).empty());
	CHECK(!e.takeSongEnd());
	// Playing into the end: notes up to it, then nothing.
	e.jump(9.0 - 1e-9);
	v = onTimes(e, 9.0 - 1e-9, 20.0);
	CHECK_EQ(v.size(), size_t(2)); // 10 (entry 2), 12 (entry 3), then the end at 16
	CHECK(e.songOver());
}

TEST_CASE(song_timeline_loops_and_turns_on_at_the_bar_line) {
	Engine e;
	timelineSong(e);
	e.song.loop = true;
	EventList out;
	onTimes(e, -1e-9, 5.0);
	e.setSongOn(true, 5.0, out); // mid-bar 2: the timeline's bar 3 (entry 2) at 8
	CHECK(e.songPending());
	CHECK(near(e.songPendingTime(), 8.0));
	onTimes(e, 5.0, 8.5);
	CHECK_EQ(e.songEntry(), 1);
	CHECK_EQ(e.currentPattern(), 1);
	// Looping: bar 5 (time 16) is entry 1 again, bar 7 (24) entry 2.
	onTimes(e, 8.5, 16.5);
	CHECK_EQ(e.songEntry(), 0);
	CHECK(near(e.songEntryStart(), 16.0));
	e.jump(25.0 - 1e-9);
	CHECK_EQ(e.songEntry(), 1);
	CHECK(near(e.songEntryStart(), 24.0));
}

TEST_CASE(song_timeline_override_stays_until_resumed) {
	Engine e;
	timelineSong(e);
	e.storePattern(3, pulses(8));
	startAtZero(e);
	onTimes(e, -1e-9, 1.0);
	EventList out;
	e.queuePattern(3, 1.0, out, true);
	CHECK(e.songOverridden());
	e.jump(8.0 - 1e-9); // the transport moves: the player's pattern stays
	CHECK(e.songOverridden());
	CHECK_EQ(e.currentPattern(), 3);
	onTimes(e, 8.0 - 1e-9, 9.5);
	e.resumeSong(9.5); // back to the song: the timeline's bar 4 (entry 3) at 12
	CHECK(near(e.songPendingTime(), 12.0));
	onTimes(e, 9.5, 12.5);
	CHECK(!e.songOverridden());
	CHECK_EQ(e.songEntry(), 2);
	CHECK_EQ(e.currentPattern(), 2);
	// A resume waiting when the transport jumps: there at once.
	e.queuePattern(3, 12.5, out, true);
	e.resumeSong(12.5);
	e.jump(4.0 - 1e-9);
	CHECK_EQ(e.songEntry(), 0);
	CHECK(near(e.songEntryStart(), 0.0));
}

TEST_CASE(song_timeline_edits_land_on_the_next_bar_line) {
	Engine e;
	timelineSong(e);
	startAtZero(e);
	onTimes(e, -1e-9, 1.0);
	// A later entry's bars: entry 1 is still where it was; no switch, nothing cut.
	uint32_t switches = e.patternSwitches();
	e.song.entries[2].bars = 4;
	e.songEdited(1.0);
	CHECK(e.songPending());
	std::vector<double> v = onTimes(e, 1.0, 4.5);
	CHECK_EQ(v.size(), size_t(3)); // 2, 3, 4
	CHECK_EQ(e.songEntry(), 0);
	CHECK(e.patternSwitches() == switches);
	// The first entry shortened to 1 bar: bar 3 is now entry 3's, from the next bar line.
	e.song.entries[0].bars = 1;
	e.songEdited(4.5);
	onTimes(e, 4.5, 7.5);
	CHECK_EQ(e.currentPattern(), 0);
	onTimes(e, 7.5, 8.5);
	CHECK_EQ(e.songEntry(), 2);
	CHECK_EQ(e.currentPattern(), 2);
	CHECK(near(e.songEntryStart(), 8.0));
}

TEST_CASE(song_timeline_override_past_the_end) {
	Engine e;
	timelineSong(e);
	e.storePattern(3, pulses(4));
	startAtZero(e);
	e.jump(20.0 - 1e-9);
	CHECK(e.songOver());
	EventList out;
	e.queuePattern(3, 20.0 - 1e-9, out, true);
	CHECK(!e.songOver());
	CHECK(e.songOverridden());
	CHECK_EQ(int(onTimes(e, 20.0 - 1e-9, 24.0 - 1e-6).size()), 4);
	e.setSongOn(false, 24.0, out);
	CHECK(!e.songOver());
}
