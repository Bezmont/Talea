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
};

/** Note-ons of track 0 in (from, to], in windows of `window`. */
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
				out.push_back({n.time, n.velocity});
		}
	}
	return out;
}

bool same(const std::vector<On>& a, const std::vector<On>& b) {
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); i++) {
		if (std::fabs(a[i].time - b[i].time) > 1e-9 || a[i].velocity != b[i].velocity)
			return false;
	}
	return true;
}

/** Track 0: a note every 1/16 at velocity 64. */
void everyStep(Engine& e) {
	e.pattern.tracks[0].set(TP_PULSES, 16);
	e.pattern.tracks[0].set(TP_VELOCITY, 64);
}

} // namespace

TEST_CASE(random_velocity_moves_one_way_and_replays) {
	Engine a;
	everyStep(a);
	a.pattern.random[0].amount[TP_VELOCITY] = 50; // CW: up to +63
	std::vector<On> notes = ons(a, -1e-9, 16.0);
	CHECK_EQ(notes.size(), size_t(65));
	bool varies = false;
	for (const On& n : notes) {
		CHECK(n.velocity >= 64 && n.velocity <= 128);
		varies |= n.velocity != 64;
	}
	CHECK(varies);
	// Another window size and a fresh engine give the same notes.
	Engine b;
	everyStep(b);
	b.pattern.random[0].amount[TP_VELOCITY] = 50;
	CHECK(same(notes, ons(b, -1e-9, 16.0, 0.7)));

	Engine c;
	everyStep(c);
	c.pattern.random[0].amount[TP_VELOCITY] = -50; // CCW: only down
	for (const On& n : ons(c, -1e-9, 4.0))
		CHECK(n.velocity <= 64);
}

TEST_CASE(random_sequence_repeats_without_variance) {
	Engine e;
	everyStep(e);
	e.pattern.random[0].amount[TP_VELOCITY] = 100;
	// Rate 1/16: 16 ticks = 4 quarters, so each bar plays the same 16 values.
	std::vector<On> notes = ons(e, -1e-9, 12.0);
	for (int i = 0; i < 16; i++)
		CHECK(notes[i].velocity == notes[i + 16].velocity && notes[i].velocity == notes[i + 32].velocity);

	Engine v;
	everyStep(v);
	v.pattern.random[0].amount[TP_VELOCITY] = 100;
	v.pattern.random[0].variance = 100;
	std::vector<On> drift = ons(v, -1e-9, 8.0);
	int changed = 0;
	for (int i = 0; i < 16; i++)
		changed += drift[i].velocity != drift[i + 16].velocity;
	CHECK(changed > 4);
	for (int i = 0; i < 16; i++)
		CHECK_EQ(drift[i].velocity, notes[i].velocity); // the first pass is the same sequence
}

TEST_CASE(random_variance_drifts_gradually) {
	TrackRandom r;
	r.reset(0);
	r.variance = 25;
	int changed = 0, total = 0;
	for (long pass = 1; pass < 200; pass++) {
		for (int slot = 0; slot < kRandomSequence; slot++) {
			long u = pass * kRandomSequence + slot;
			changed += r.draw(TP_VELOCITY, u) != r.draw(TP_VELOCITY, u - kRandomSequence);
			total++;
		}
	}
	double rate = double(changed) / total;
	CHECK(rate > 0.2 && rate < 0.3);
}

TEST_CASE(random_rate_holds_values) {
	Engine e;
	everyStep(e);
	e.pattern.random[0].amount[TP_VELOCITY] = 100;
	e.pattern.random[0].rate = 7; // 1/8: pairs of 1/16 notes share a value
	std::vector<On> notes = ons(e, -1e-9, 4.0);
	for (int i = 0; i + 1 < 16; i += 2)
		CHECK_EQ(notes[i].velocity, notes[i + 1].velocity);
}

TEST_CASE(random_pulses_follow_cycles) {
	Engine e;
	e.pattern.tracks[0].set(TP_PULSES, 8);
	TrackRandom& r = e.pattern.random[0];
	r.amount[TP_PULSES] = -100; // per cycle (default unit): 0..16 fewer pulses each loop
	CHECK(r.isPerCycle(TP_PULSES));
	std::vector<On> notes = ons(e, -1e-9, 64.0 - 1e-6);
	int perLoop[16] = {};
	for (const On& n : notes)
		perLoop[int(n.time / 4.0)]++;
	bool varies = false;
	for (int i = 0; i < 16; i++) {
		CHECK(perLoop[i] <= 8);
		varies |= perLoop[i] != perLoop[0];
	}
	CHECK(varies);
}

TEST_CASE(random_skips_loop_length_params) {
	Engine e;
	everyStep(e);
	TrackRandom& r = e.pattern.random[0];
	r.amount[TP_STEPS] = r.amount[TP_DIVISION] = r.amount[TP_LENGTH] = 100;
	CHECK(!r.active());
	CHECK_EQ(ons(e, -1e-9, 8.0).size(), size_t(33));
	CHECK(std::fabs(e.patternQuarters() - 4.0) < 1e-9);
	Pattern p;
	CHECK(patternFromJson(patternToJson(e.pattern), p));
	CHECK_EQ(p.random[0].amount[TP_STEPS], 0.f);
}

TEST_CASE(random_rotate_wraps) {
	Track t;
	t.set(TP_STEPS, 8);
	t.set(TP_ROTATE, 6);
	TrackRandom r;
	r.reset(0);
	r.amount[TP_ROTATE] = 100;
	for (long loop = 0; loop < 64; loop++) {
		Track x = t;
		r.apply(x, loop, 0.0);
		CHECK(x.getInt(TP_ROTATE) >= 0 && x.getInt(TP_ROTATE) < 8);
	}
}

TEST_CASE(random_state_round_trip) {
	Pattern a;
	TrackRandom& r = a.random[5];
	r.amount[TP_VELOCITY] = -30;
	r.amount[TP_PULSES] = 70;
	r.perCycle ^= 1u << TP_VELOCITY;
	r.variance = 12.5f;
	r.rate = 3;
	r.seed = 99;
	Pattern b;
	CHECK(patternFromJson(patternToJson(a), b));
	const TrackRandom& c = b.random[5];
	CHECK(c.amount[TP_VELOCITY] == -30.f && c.amount[TP_PULSES] == 70.f && c.amount[TP_PITCH] == 0.f);
	CHECK(c.isPerCycle(TP_VELOCITY) && c.variance == 12.5f && c.rate == 3 && c.seed == 99u);
	CHECK_EQ(b.random[0].seed, a.random[0].seed);
}

TEST_CASE(random_amounts_per_cycle) {
	Engine e;
	everyStep(e);
	TrackCycles& cyc = e.pattern.cycles[0];
	cyc.count = 2;
	cyc.setRandom(1, TP_VELOCITY, 100); // only cycle 2 is random
	cyc.setRandom(1, TP_STEPS, 100);    // ignored: not randomisable
	CHECK(!cyc.cycles[1].hasRandom(TP_STEPS));
	std::vector<On> notes = ons(e, -1e-9, 8.0 - 1e-6);
	CHECK_EQ(notes.size(), size_t(32));
	bool varies = false;
	for (int i = 0; i < 16; i++) {
		CHECK_EQ(notes[i].velocity, 64);
		varies |= notes[16 + i].velocity != 64;
	}
	CHECK(varies);
	// A track amount reaches cycles without their own; an override of 0 turns it off.
	e.pattern.random[0].amount[TP_VELOCITY] = 100;
	cyc.setRandom(1, TP_VELOCITY, 0);
	notes = ons(e, 8.0 - 1e-6, 16.0 - 1e-6);
	CHECK_EQ(notes.size(), size_t(32));
	bool first = false;
	for (int i = 0; i < 16; i++) {
		first |= notes[i].velocity != 64;
		CHECK_EQ(notes[16 + i].velocity, 64);
	}
	CHECK(first);
	CHECK_EQ(cyc.randomAmount(e.pattern.random[0], 0, TP_VELOCITY), 100.f);
	cyc.cycles[1].clearChanges();
	CHECK(cyc.cycles[1].empty());
}

TEST_CASE(random_cycle_amounts_round_trip) {
	Pattern a;
	a.cycles[2].count = 3;
	a.cycles[2].setRandom(2, TP_PITCH, -45);
	Pattern b;
	CHECK(patternFromJson(patternToJson(a), b));
	CHECK(b.cycles[2].cycles[2].hasRandom(TP_PITCH) && b.cycles[2].cycles[2].randomAmounts[TP_PITCH] == -45.f);
	CHECK(!b.cycles[2].cycles[2].hasRandom(TP_VELOCITY));
}

TEST_CASE(random_phase_shifts_and_regenerate_changes) {
	TrackRandom r;
	r.reset(0);
	TrackRandom shifted = r;
	shifted.phase = 3;
	for (long u = 0; u < 40; u++)
		CHECK_EQ(shifted.draw(TP_VELOCITY, shifted.position(TP_VELOCITY, u)), r.draw(TP_VELOCITY, u + 3));
	TrackRandom fresh = r;
	fresh.regenerate();
	CHECK(fresh.seed != r.seed);
	int same = 0;
	for (long u = 0; u < 16; u++)
		same += fresh.draw(TP_VELOCITY, u) == r.draw(TP_VELOCITY, u);
	CHECK(same < 4);
}

TEST_CASE(random_lock_freezes_variance) {
	Engine e;
	everyStep(e);
	TrackRandom& r = e.pattern.random[0];
	r.amount[TP_VELOCITY] = 100;
	r.variance = 100; // every pass new
	e.lockRandom(0, 4.5); // in the second bar: pass 1 of the 1/16 sequence
	CHECK(r.locked && r.lockTick == 18);
	std::vector<On> notes = ons(e, -1e-9, 16.0 - 1e-6);
	CHECK_EQ(notes.size(), size_t(64));
	for (int i = 0; i < 16; i++) {
		CHECK_EQ(notes[i].velocity, notes[16 + i].velocity);
		CHECK_EQ(notes[i].velocity, notes[48 + i].velocity);
	}
	Engine u;
	everyStep(u);
	u.pattern.random[0].amount[TP_VELOCITY] = 100;
	u.pattern.random[0].variance = 100;
	std::vector<On> free = ons(u, -1e-9, 8.0 - 1e-6);
	for (int i = 0; i < 16; i++)
		CHECK_EQ(notes[i].velocity, free[16 + i].velocity); // the pass it was locked in
}

TEST_CASE(random_slew_glides_between_values) {
	TrackRandom r;
	r.reset(0);
	r.amount[TP_VELOCITY] = 100;
	r.rate = 9; // 1/16 = 0.25 quarters
	r.slew = 100;
	Track t;
	t.set(TP_VELOCITY, 0);
	auto at = [&](double time) {
		Track x = t;
		r.apply(x, 0, time);
		return x.get(TP_VELOCITY);
	};
	TrackRandom hard = r;
	hard.slew = 0;
	auto hardAt = [&](double time) {
		Track x = t;
		hard.apply(x, 0, time);
		return x.get(TP_VELOCITY);
	};
	CHECK_EQ(at(0.0), hardAt(0.0));
	CHECK_EQ(at(0.25), hardAt(0.25));
	float mid = at(0.125), a = hardAt(0.0), b = hardAt(0.25);
	CHECK(mid >= std::min(a, b) - 1.f && mid <= std::max(a, b) + 1.f);
	CHECK(std::fabs(mid - (a + b) / 2) <= 1.f);
}

TEST_CASE(random_extras_round_trip) {
	Pattern a;
	TrackRandom& r = a.random[2];
	r.phase = -2;
	r.probPhase = 5;
	r.slew = 30;
	r.locked = true;
	r.lockLoop = 7;
	r.lockTick = 99;
	Pattern b;
	CHECK(a != b);
	CHECK(patternFromJson(patternToJson(a), b));
	CHECK(a == b);
}

TEST_CASE(probability_phase_moves_skips) {
	auto skipped = [](int phase) {
		Engine e;
		everyStep(e);
		e.pattern.tracks[0].set(TP_PROBABILITY, 50);
		e.pattern.random[0].probPhase = phase;
		std::vector<bool> played(16, false);
		for (const On& n : ons(e, -1e-9, 4.0 - 1e-6))
			played[int(std::lround(n.time * 4))] = true;
		return played;
	};
	std::vector<bool> a = skipped(0), b = skipped(1);
	for (int i = 1; i < 16; i++)
		CHECK_EQ(b[i], a[i - 1]);
}
