#include "Test.hpp"
#include "core/Engine.hpp"

using namespace talea;

TEST_CASE(temp_reverts_edits_but_not_mutes) {
	Engine e;
	e.pattern.tracks[0].set(TP_PULSES, 4);
	e.beginTemp();
	CHECK(e.tempActive());
	e.pattern.tracks[0].set(TP_PULSES, 9);
	e.pattern.cycles[0].count = 3;
	e.pattern.random[0].amount[TP_VELOCITY] = 50;
	EventList out;
	e.setMuted(2, true, 0.0, out);
	CHECK_EQ(e.committedPattern().tracks[0].getInt(TP_PULSES), 4);
	e.beginTemp(); // already active: keeps the first snapshot
	e.endTemp(false);
	CHECK(!e.tempActive());
	CHECK_EQ(e.pattern.tracks[0].getInt(TP_PULSES), 4);
	CHECK_EQ(e.pattern.cycles[0].count, 1);
	CHECK_EQ(e.pattern.random[0].amount[TP_VELOCITY], 0.f);
	CHECK(e.pattern.tracks[2].muted);
}

TEST_CASE(temp_keep_commits_edits) {
	Engine e;
	e.beginTemp();
	e.pattern.tracks[1].set(TP_STEPS, 12);
	e.endTemp(true);
	CHECK_EQ(e.pattern.tracks[1].steps(), 12);
	CHECK_EQ(e.committedPattern().tracks[1].steps(), 12);
	e.beginTemp();
	e.pattern.tracks[1].set(TP_STEPS, 5);
	e.dropTemp(); // e.g. a patch loaded over it: nothing to revert to
	e.endTemp(false);
	CHECK_EQ(e.pattern.tracks[1].steps(), 5);
}
