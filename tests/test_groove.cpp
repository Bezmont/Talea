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

std::vector<NoteEvent> run(Engine& e, double from, double to, double window) {
	std::vector<NoteEvent> all;
	EventList list;
	for (double t = from; t < to - 1e-12; t += window) {
		list.clear();
		e.process(t, std::min(t + window, to), list);
		all.insert(all.end(), list.begin(), list.end());
	}
	return all;
}

std::vector<NoteEvent> ons(const std::vector<NoteEvent>& events) {
	std::vector<NoteEvent> out;
	for (const NoteEvent& e : events) {
		if (e.on)
			out.push_back(e);
	}
	return out;
}

bool near(double a, double b) {
	return std::fabs(a - b) < 1e-6;
}

} // namespace

TEST_CASE(probability_centre_plays_everything) {
	Engine e;
	e.pattern.tracks[0].set(TP_PULSES, 16);
	CHECK_EQ(int(ons(run(e, -1e-9, 16.0 - 1e-6, 1.0)).size()), 64);
}

TEST_CASE(probability_skips_a_repeatable_share) {
	auto build = [](Engine& e, float p) {
		e.pattern.tracks[0].set(TP_PULSES, 16);
		e.pattern.tracks[0].set(TP_PROBABILITY, p);
	};
	Engine a, b, full;
	build(a, -50);
	build(b, -50);
	build(full, -100);
	std::vector<NoteEvent> first = ons(run(a, -1e-9, 64.0, 1.0));
	std::vector<NoteEvent> second = ons(run(b, -1e-9, 64.0, 0.01));
	// ~half of 256 steps play, and the same ones every time
	CHECK(first.size() > 96 && first.size() < 160);
	CHECK_EQ(first.size(), second.size());
	for (size_t i = 0; i < first.size() && i < second.size(); i++)
		CHECK(near(first[i].time, second[i].time));
	CHECK_EQ(int(ons(run(full, -1e-9, 64.0, 1.0)).size()), 0);
}

TEST_CASE(probability_ccw_skips_pulses_only) {
	auto count = [](float p) {
		Engine e;
		Track& t = e.pattern.tracks[0];
		t.set(TP_PULSES, 4);
		t.set(TP_REPEATS, 3); // 1/16 repeats fill each quarter
		t.set(TP_PROBABILITY, p);
		std::vector<NoteEvent> on = ons(run(e, -1e-9, 64.0, 0.5));
		// Notes per pulse position: CCW keeps whole bursts (0 or 4 per quarter).
		std::vector<int> perBeat(64, 0);
		for (const NoteEvent& n : on)
			perBeat[std::min(63, int(std::floor(n.time + 1e-9)))]++;
		return perBeat;
	};
	for (int n : count(-50))
		CHECK(n == 0 || n == 4);
	bool partial = false;
	for (int n : count(50))
		partial = partial || (n > 0 && n < 4);
	CHECK(partial); // CW thins out repeats too
}

TEST_CASE(timing_swings_every_second_step) {
	Engine e;
	Track& t = e.pattern.tracks[0];
	t.set(TP_PULSES, 16);
	t.set(TP_TIMING, 100);
	std::vector<NoteEvent> on = ons(run(e, -1e-9, 1.0 - 1e-6, 0.03));
	CHECK_EQ(int(on.size()), 4);
	const double expect[] = {0.0, 0.375, 0.5, 0.875}; // odd 1/16s pushed half a step
	for (int i = 0; i < 4 && i < int(on.size()); i++)
		CHECK(near(on[i].time, expect[i]));

	t.swingSubdivision = 1; // x2: every 2nd 1/8 moves by half an 1/8
	on = ons(run(e, 1.0 - 1e-6, 2.0 - 1e-6, 0.03));
	CHECK_EQ(int(on.size()), 4);
	const double expect2[] = {1.0, 1.25, 1.75, 1.75};
	for (int i = 0; i < 4 && i < int(on.size()); i++)
		CHECK(near(on[i].time, expect2[i]));
}

TEST_CASE(delay_moves_whole_track_both_ways) {
	Engine late;
	late.pattern.tracks[0].set(TP_PULSES, 4);
	late.pattern.tracks[0].set(TP_DELAY, 50); // half a 1/16 later
	std::vector<NoteEvent> on = ons(run(late, -1e-9, 4.0 - 1e-6, 0.1));
	CHECK_EQ(int(on.size()), 4);
	CHECK(on.size() == 4 && near(on[0].time, 0.125) && near(on[3].time, 3.125));

	// Earlier: the very first downbeat would be before the start and is dropped; the next
	// loop's downbeat arrives early, at the end of this one.
	Engine early;
	early.pattern.tracks[0].set(TP_PULSES, 4);
	early.pattern.tracks[0].set(TP_DELAY, -100);
	on = ons(run(early, -1e-9, 4.0 - 1e-6, 0.1));
	CHECK_EQ(int(on.size()), 4);
	CHECK(on.size() == 4 && near(on[0].time, 0.75) && near(on[3].time, 3.75));
}

TEST_CASE(groove_window_size_invariance) {
	auto build = [](Engine& e) {
		Track& t = e.pattern.tracks[0];
		t.set(TP_PULSES, 11);
		t.set(TP_TIMING, -60);
		t.set(TP_DELAY, -35);
		t.set(TP_PROBABILITY, 30);
		t.set(TP_REPEATS, 2);
		t.set(TP_REPEAT_TIME, indexOf(kRepeatTimes, "1/32"));
		t.set(TP_ACCENT, 70);
		t.swingSubdivision = 2;
		t.groove = GROOVE_SINE;
		Track& u = e.pattern.tracks[1];
		u.set(TP_PULSES, 5);
		u.set(TP_DIVISION, indexOf(kDivisions, "1/8t"));
		u.set(TP_DELAY, 80);
	};
	Engine a, b;
	build(a);
	build(b);
	std::vector<NoteEvent> big = run(a, -1e-9, 32.0, 32.0);
	std::vector<NoteEvent> small = run(b, -1e-9, 32.0, 2.0 / 48000.0);
	CHECK_EQ(big.size(), small.size());
	for (size_t i = 0; i < big.size() && i < small.size(); i++) {
		CHECK(near(big[i].time, small[i].time));
		CHECK_EQ(big[i].on, small[i].on);
		CHECK_EQ(big[i].velocity, small[i].velocity);
	}
}

TEST_CASE(accent_follows_groove_shape) {
	Track t;
	t.set(TP_ACCENT, 100);
	t.groove = GROOVE_PULSE;
	t.grooveLength = 0; // 1 bar
	CHECK_EQ(t.accented(100, 0.5), 127);         // first half: up (clamped)
	CHECK_EQ(t.accented(100, 2.5), 100 - 63);    // second half: down
	t.set(TP_ACCENT, 0);
	CHECK_EQ(t.accented(100, 2.5), 100);         // no accent, no change
	t.set(TP_ACCENT, 50);
	t.groove = GROOVE_SAW;
	t.grooveLength = 2;                          // 4 bars = 16 quarters
	CHECK_EQ(t.accented(64, 8.0), 64);           // saw crosses zero halfway
	CHECK(t.accented(64, 15.9) > t.accented(64, 4.0));
}

TEST_CASE(groove_state_round_trip) {
	Pattern a;
	a.tracks[2].groove = GROOVE_TRIANGLE;
	a.tracks[2].grooveLength = 5;
	a.tracks[2].swingSubdivision = 2;
	Pattern b;
	CHECK(patternFromJson(patternToJson(a), b));
	CHECK_EQ(b.tracks[2].groove, int(GROOVE_TRIANGLE));
	CHECK_EQ(b.tracks[2].grooveLength, 5);
	CHECK_EQ(b.tracks[2].swingSubdivision, 2);
	CHECK_EQ(b.tracks[3].groove, int(GROOVE_AGOGO));
}
