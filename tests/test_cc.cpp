#include "Test.hpp"
#include "core/Engine.hpp"
#include "core/State.hpp"

#include <cmath>
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

/** Track 0 as a CC track: 4 pulses of 16 steps (steps 0, 4, 8, 12). */
void ccTrack(Engine& e) {
	e.pattern.tracks[0].cc = true;
	e.pattern.tracks[0].ccNumber = 74;
	e.pattern.tracks[0].set(TP_PULSES, 4);
}

} // namespace

TEST_CASE(cc_track_values_follow_velocity_and_locks) {
	Engine e;
	ccTrack(e);
	e.pattern.tracks[0].set(TP_VELOCITY, 40);
	e.pattern.locks[0].set(4, TP_VELOCITY, 0);
	std::vector<NoteEvent> v = ons(e, -1e-9, 4.0 - 1e-6);
	CHECK_EQ(v.size(), size_t(4));
	CHECK(v[0].cc);
	CHECK_EQ(v[0].ccNumber, 74);
	CHECK_EQ(v[0].velocity, 40);
	CHECK_EQ(v[1].velocity, 0); // a CC value can be 0
	CHECK_EQ(v[2].velocity, 40);
}

TEST_CASE(cc_track_glide_follows_sustain) {
	Engine e;
	ccTrack(e);
	e.pattern.tracks[0].set(TP_SUSTAIN, 0);
	CHECK_EQ(ons(e, -1e-9, 0.5)[0].glide, 0.0);

	Engine f;
	ccTrack(f);
	f.pattern.tracks[0].set(TP_SUSTAIN, 50); // half of the 1/16 step
	CHECK(std::fabs(ons(f, -1e-9, 0.5)[0].glide - 0.125) < 1e-9);
}

TEST_CASE(cc_ramp_reaches_zero) {
	Track t;
	t.cc = true;
	t.set(TP_REPEATS, 4);
	t.set(TP_RAMP, -100);
	t.set(TP_VELOCITY, 100);
	CHECK_EQ(t.repeatVelocity(4), 0);
	t.cc = false;
	CHECK_EQ(t.repeatVelocity(4), 1);
}

TEST_CASE(note_tracks_send_no_cc) {
	Engine e;
	e.pattern.tracks[0].set(TP_PULSES, 4);
	CHECK(!ons(e, -1e-9, 0.5)[0].cc);
}

TEST_CASE(cc_state_round_trip) {
	Pattern a;
	a.tracks[5].cc = true;
	a.tracks[5].ccNumber = 119;
	Pattern b;
	CHECK(patternFromJson(patternToJson(a), b));
	CHECK(b.tracks[5].cc);
	CHECK_EQ(b.tracks[5].ccNumber, 119);
	CHECK(!b.tracks[4].cc);
	CHECK(a == b);
	CHECK(a.trackUsed(5));
}
