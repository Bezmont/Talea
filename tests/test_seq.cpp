#include "Test.hpp"
#include "core/Engine.hpp"
#include "core/Record.hpp"
#include "core/State.hpp"

#include <cmath>
#include <vector>

using namespace talea;

// SEQ mode: a track (or cycle) plays a manual sequence instead of its Euclidean hits.

namespace {

std::vector<NoteEvent> trackEvents(Engine& e, int track, double to) {
	to -= 1e-6; // a window includes its end
	std::vector<NoteEvent> out;
	EventList list;
	for (double t = -1e-9; t < to - 1e-12; t += 0.5) {
		list.clear();
		e.process(t, std::min(t + 0.5, to), list);
		for (const NoteEvent& n : list) {
			if (n.track == track)
				out.push_back(n);
		}
	}
	return out;
}

std::vector<NoteEvent> onsOf(const std::vector<NoteEvent>& events) {
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

void note(StepSequence& sq, int pos, std::vector<int> notes, int velocity = 100, int gate = 1) {
	sq.set(pos, notes.data(), int(notes.size()), velocity, gate);
}

} // namespace

// The sequence itself.

TEST_CASE(seq_step_sorts_dedupes_and_clamps) {
	StepSequence sq;
	note(sq, 3, {67, 60, 67, 200, -4}, 300, 99);
	CHECK(sq.has(3));
	CHECK_EQ(int(sq.steps[3].count), 4);
	CHECK_EQ(int(sq.steps[3].notes[0]), 0);
	CHECK_EQ(int(sq.steps[3].notes[1]), 60);
	CHECK_EQ(int(sq.steps[3].notes[2]), 67);
	CHECK_EQ(int(sq.steps[3].notes[3]), 127);
	CHECK_EQ(int(sq.steps[3].velocity), 127);
	CHECK_EQ(int(sq.steps[3].gate), kMaxGateSteps);
	note(sq, 3, {});
	CHECK(!sq.has(3)); // no notes = a rest
	CHECK(sq.empty());
}

TEST_CASE(seq_toggle_note_builds_and_empties_a_step) {
	StepSequence sq;
	sq.toggleNote(0, 60, 90);
	CHECK(sq.has(0));
	CHECK_EQ(int(sq.steps[0].velocity), 90);
	sq.toggleNote(0, 64, 50); // joins, keeps the step's velocity
	CHECK_EQ(int(sq.steps[0].count), 2);
	CHECK_EQ(int(sq.steps[0].velocity), 90);
	sq.toggleNote(0, 60, 0);
	sq.toggleNote(0, 64, 0);
	CHECK(!sq.has(0)); // the last note out leaves a rest
}

TEST_CASE(seq_add_note_stops_at_the_chord_limit) {
	StepSequence sq;
	for (int i = 0; i < kMaxPool; i++)
		CHECK(sq.addNote(0, 60 + i, 100));
	CHECK(!sq.addNote(0, 90, 100));
	CHECK(!sq.addNote(0, 60, 100)); // already there
	CHECK_EQ(int(sq.steps[0].count), kMaxPool);
}

// Playing it.

TEST_CASE(seq_hits_are_the_steps_with_notes) {
	Engine e;
	Track& t = e.pattern.tracks[0];
	t.seqMode = true;
	t.set(TP_STEPS, 8);
	t.set(TP_PULSES, 8);  // ignored
	t.set(TP_ROTATE, 3);  // ignored
	note(e.pattern.seqs[0], 0, {60});
	note(e.pattern.seqs[0], 5, {62});
	note(e.pattern.seqs[0], 9, {64}); // past Steps: never plays
	CHECK_EQ(e.hits(0), (StepMask(1) << 0) | (StepMask(1) << 5));
	std::vector<NoteEvent> on = onsOf(trackEvents(e, 0, 2.0)); // one loop of 8 x 1/16
	CHECK_EQ(int(on.size()), 2);
	if (on.size() == 2) {
		CHECK(near(on[0].time, 0.0));
		CHECK_EQ(on[0].note, 60);
		CHECK(near(on[1].time, 5 * 0.25));
		CHECK_EQ(on[1].note, 62);
	}
}

TEST_CASE(seq_notes_play_as_entered_without_voicing_or_phrase) {
	Engine e;
	Track& t = e.pattern.tracks[0];
	t.seqMode = true;
	t.set(TP_VOICING, 5);
	t.set(TP_RANGE, 12);
	t.phrase = PHRASE_PULSE;
	note(e.pattern.seqs[0], 0, {61, 66, 70});
	std::vector<NoteEvent> on = onsOf(trackEvents(e, 0, 1.0));
	CHECK_EQ(int(on.size()), 1);
	if (!on.empty()) {
		CHECK_EQ(on[0].chordSize, 3);
		CHECK_EQ(int(on[0].chord[0]), 61);
		CHECK_EQ(int(on[0].chord[1]), 66);
		CHECK_EQ(int(on[0].chord[2]), 70);
	}
}

TEST_CASE(seq_notes_off_the_scale_play_snapped_up) {
	Engine e;
	Track& t = e.pattern.tracks[0];
	t.seqMode = true;
	t.scale = 1; // C major: C#, F#, A# are ties, so they go up
	note(e.pattern.seqs[0], 0, {61, 66, 70});
	std::vector<NoteEvent> on = onsOf(trackEvents(e, 0, 1.0));
	CHECK_EQ(int(on.size()), 1);
	if (!on.empty()) {
		CHECK_EQ(on[0].chordSize, 3);
		CHECK_EQ(int(on[0].chord[0]), 62);
		CHECK_EQ(int(on[0].chord[1]), 67);
		CHECK_EQ(int(on[0].chord[2]), 71);
	}
}

TEST_CASE(seq_pitch_and_transpose_still_apply) {
	Engine e;
	Track& t = e.pattern.tracks[0];
	t.seqMode = true;
	t.set(TP_PITCH, 2);
	e.transpose[0] = 12;
	note(e.pattern.seqs[0], 0, {60});
	std::vector<NoteEvent> on = onsOf(trackEvents(e, 0, 1.0));
	CHECK_EQ(int(on.size()), 1);
	if (!on.empty())
		CHECK_EQ(on[0].note, 74);
}

TEST_CASE(seq_velocity_is_the_steps_under_its_locks) {
	Engine e;
	Track& t = e.pattern.tracks[0];
	t.seqMode = true;
	t.set(TP_VELOCITY, 10); // the knob doesn't set it
	note(e.pattern.seqs[0], 0, {60}, 77);
	note(e.pattern.seqs[0], 1, {60}, 33);
	e.pattern.locks[0].set(1, TP_VELOCITY, 120); // a lock still overrides
	std::vector<NoteEvent> on = onsOf(trackEvents(e, 0, 1.0));
	CHECK_EQ(int(on.size()), 2);
	if (on.size() == 2) {
		CHECK_EQ(on[0].velocity, 77);
		CHECK_EQ(on[1].velocity, 120);
	}
}

TEST_CASE(seq_gate_counts_steps) {
	Engine e;
	Track& t = e.pattern.tracks[0];
	t.seqMode = true;
	note(e.pattern.seqs[0], 0, {60}, 100, 3); // held 3 x 1/16
	note(e.pattern.seqs[0], 8, {62}, 100, 1);
	std::vector<NoteEvent> ev = trackEvents(e, 0, 4.0);
	double off0 = -1.0, off8 = -1.0;
	for (const NoteEvent& n : ev) {
		if (!n.on && n.note == 60)
			off0 = n.time;
		if (!n.on && n.note == 62)
			off8 = n.time;
	}
	CHECK(near(off0, 0.75));
	CHECK(near(off8, 2.25));
}

TEST_CASE(seq_tied_note_is_cut_by_the_next_step_with_notes) {
	Engine e;
	Track& t = e.pattern.tracks[0];
	t.seqMode = true;
	note(e.pattern.seqs[0], 0, {60}, 100, 8);
	note(e.pattern.seqs[0], 2, {64});
	std::vector<NoteEvent> ev = trackEvents(e, 0, 1.0);
	double off = -1.0;
	for (const NoteEvent& n : ev) {
		if (!n.on && n.note == 60)
			off = n.time;
	}
	CHECK(near(off, 0.5));
}

// Cycles: copies of the track's sequence until edited.

TEST_CASE(seq_cycles_play_the_track_sequence_until_edited) {
	Pattern p;
	note(p.seqs[0], 0, {60});
	p.cycles[0].count = 2;
	CHECK(&p.sequenceOf(0, 1) == &p.seqs[0]);
	StepSequence& own = p.sequenceEdit(0, 1);
	CHECK(p.cycles[0].cycles[1].ownSeq);
	CHECK(own == p.seqs[0]); // starts as a copy
	note(own, 4, {72});
	CHECK(p.sequenceOf(0, 1).has(4));
	CHECK(!p.seqs[0].has(4));        // the track's is untouched
	CHECK(!p.sequenceOf(0, 0).has(4)); // and cycle 1 still plays it
	note(p.seqs[0], 6, {65});
	CHECK(!p.sequenceOf(0, 1).has(6)); // an edited cycle no longer follows the track
}

TEST_CASE(seq_clearing_a_cycle_goes_back_to_the_track_sequence) {
	Pattern p;
	note(p.seqs[0], 0, {60});
	note(p.sequenceEdit(0, 1), 3, {70});
	CHECK(p.cycles[0].cycles[1].modified());
	p.cycles[0].cycles[1].clearChanges();
	CHECK(!p.cycles[0].cycles[1].ownSeq);
	CHECK(&p.sequenceOf(0, 1) == &p.seqs[0]);
	note(p.sequenceEdit(0, 2), 3, {70});
	p.cycles[0].cycles[2].clearSequence();
	CHECK(!p.cycles[0].cycles[2].modified());
}

TEST_CASE(seq_cycle_plays_its_own_sequence) {
	Engine e;
	Track& t = e.pattern.tracks[0];
	t.seqMode = true;
	t.set(TP_STEPS, 4);
	e.pattern.cycles[0].count = 2;
	note(e.pattern.seqs[0], 0, {60});
	note(e.pattern.sequenceEdit(0, 1), 0, {72});
	std::vector<NoteEvent> on = onsOf(trackEvents(e, 0, 2.0)); // two loops of 4 x 1/16
	CHECK_EQ(int(on.size()), 2);
	if (on.size() == 2) {
		CHECK_EQ(on[0].note, 60);
		CHECK_EQ(on[1].note, 72);
	}
}

TEST_CASE(seq_mode_can_be_a_cycle_override) {
	Engine e;
	Track& t = e.pattern.tracks[0];
	t.set(TP_STEPS, 4);
	t.set(TP_PULSES, 4); // EUCLID: every step, the pool note (C4)
	TrackCycles& cyc = e.pattern.cycles[0];
	cyc.count = 2;
	cyc.cycles[1].ownMode = true;
	cyc.cycles[1].seqMode = true;
	note(e.pattern.seqs[0], 2, {70});
	CHECK(!cyc.resolve(t, 0).seqMode);
	CHECK(cyc.resolve(t, 1).seqMode);
	std::vector<NoteEvent> on = onsOf(trackEvents(e, 0, 2.0));
	CHECK_EQ(int(on.size()), 5); // 4 Euclidean pulses, then the sequence's one note
	if (on.size() == 5) {
		CHECK_EQ(on[0].note, kBaseNote);
		CHECK_EQ(on[4].note, 70);
		CHECK(near(on[4].time, 1.0 + 2 * 0.25));
	}
}

// Saving.

TEST_CASE(seq_round_trips_through_state) {
	Pattern a;
	a.tracks[2].seqMode = true;
	note(a.seqs[2], 0, {60, 64, 67}, 90, 2);
	note(a.seqs[2], 7, {72}, 40, 5);
	TrackCycles& cyc = a.cycles[2];
	cyc.count = 3;
	note(a.sequenceEdit(2, 1), 3, {50});
	cyc.cycles[2].ownMode = true;
	cyc.cycles[2].seqMode = false;
	a.sequenceEdit(2, 2).reset(); // an emptied own sequence is still its own
	Pattern b;
	CHECK(patternFromJson(patternToJson(a), b));
	CHECK(a == b);
	CHECK(b.tracks[2].seqMode);
	CHECK_EQ(int(b.seqs[2].steps[7].gate), 5);
	CHECK_EQ(int(b.seqs[2].steps[0].count), 3);
	CHECK(b.cycles[2].cycles[1].ownSeq);
	CHECK(b.cycles[2].cycles[1].seq.has(3));
	CHECK(b.cycles[2].cycles[2].ownMode);
	CHECK(b.cycles[2].cycles[2].ownSeq);
	CHECK(b.cycles[2].cycles[2].seq.empty());
}

TEST_CASE(seq_old_patterns_load_as_euclid) {
	Pattern a;
	std::string j = patternToJson(a);
	Pattern b;
	CHECK(patternFromJson(j, b));
	CHECK(!b.tracks[0].seqMode);
	CHECK(b.seqs[0].empty());
}

TEST_CASE(seq_a_sequence_makes_the_track_used) {
	Pattern a;
	CHECK(!a.trackUsed(5));
	note(a.seqs[5], 0, {60});
	CHECK(a.trackUsed(5));
	Pattern b;
	CHECK(a != b);
}

// Recording.

TEST_CASE(record_live_quantizes_to_the_nearest_step) {
	Engine e;
	e.pattern.tracks[0].seqMode = true;
	double q = 0.0;
	CHECK_EQ(e.nearestStep(0, 0.0, q), 0);
	CHECK(near(q, 0.25));
	CHECK_EQ(e.nearestStep(0, 0.37, q), 1);  // nearer 0.25 than 0.5
	CHECK_EQ(e.nearestStep(0, 0.38, q), 2);
	CHECK_EQ(e.nearestStep(0, 3.9, q), 0);   // 16 steps: past the last, the next loop's first
	e.pattern.tracks[0].set(TP_STEPS, 14);
	CHECK_EQ(e.nearestStep(0, 3.4, q), 0);   // a 3.5-quarter loop
}

TEST_CASE(record_live_writes_notes_chords_and_gates) {
	StepSequence sq;
	SeqRecorder r;
	const double q = 0.25;
	// A chord on step 4, held 3 steps (the longest note sets the gate).
	r.liveOn(sq, 4, 1.0, 60, 80);
	r.liveOn(sq, 4, 1.01, 64, 110);
	r.liveOff(sq, 60, 1.3, q);
	r.liveOff(sq, 64, 1.74, q);
	CHECK(sq.has(4));
	CHECK_EQ(int(sq.steps[4].count), 2);
	CHECK_EQ(int(sq.steps[4].velocity), 110);
	CHECK_EQ(int(sq.steps[4].gate), 3);
	// A short note still lasts a step.
	r.liveOn(sq, 9, 2.25, 70, 100);
	r.liveOff(sq, 70, 2.26, q);
	CHECK_EQ(int(sq.steps[9].gate), 1);
	// On a later pass a new note replaces the step.
	r.liveOn(sq, 4, 5.0, 67, 50);
	r.liveOff(sq, 67, 5.2, q);
	CHECK_EQ(int(sq.steps[4].count), 1);
	CHECK_EQ(int(sq.steps[4].notes[0]), 67);
	CHECK_EQ(int(sq.steps[4].gate), 1);
	CHECK_EQ(r.held(), 0);
}

TEST_CASE(record_step_writes_at_the_cursor_and_moves_on) {
	StepSequence sq;
	SeqRecorder r;
	r.stepOn(sq, 60, 100);
	r.stepOff(60, 4);
	CHECK_EQ(r.cursor, 1);
	r.stepOn(sq, 64, 70); // a chord: held together
	r.stepOn(sq, 67, 90);
	r.stepOff(64, 4);
	CHECK_EQ(r.cursor, 1); // still held
	r.stepOff(67, 4);
	CHECK_EQ(r.cursor, 2);
	CHECK_EQ(int(sq.steps[0].notes[0]), 60);
	CHECK_EQ(int(sq.steps[1].count), 2);
	CHECK_EQ(int(sq.steps[1].velocity), 90);
	r.stepOn(sq, 50, 100);
	r.stepOff(50, 4);
	r.stepOn(sq, 51, 100);
	r.stepOff(51, 4);
	CHECK_EQ(r.cursor, 0); // wraps at Steps
	r.stepOn(sq, 40, 100); // and replaces what was there
	r.stepOff(40, 4);
	CHECK_EQ(int(sq.steps[0].count), 1);
	CHECK_EQ(int(sq.steps[0].notes[0]), 40);
}

TEST_CASE(snap_up_takes_the_nearest_scale_note_ties_going_up) {
	ScaleMask major = kScales[1].mask;
	CHECK_EQ(snapUp(61, major, 0), 62); // C# between C and D: up
	CHECK_EQ(snapUp(64, major, 0), 64); // in the scale
	ScaleMask penta = 0;
	for (int pc : {0, 2, 4, 7, 9})
		penta |= ScaleMask(1u << pc);
	CHECK_EQ(snapUp(65, penta, 0), 64); // F: E is nearer than G
	CHECK_EQ(snapUp(66, penta, 0), 67); // F#: G is nearer
	CHECK_EQ(snapUp(70, penta, 0), 69); // A#: A (1) over C (2)
	CHECK_EQ(snapUp(71, penta, 0), 72); // B: C above (1) over A (2)
	CHECK_EQ(snapUp(127, major, 0), 127); // G9 is in C major
	CHECK_EQ(snapUp(61, 0xFFF, 0), 61);   // chromatic
}

TEST_CASE(seq_conform_moves_notes_onto_the_scale) {
	StepSequence sq;
	int a[] = {61, 62, 64};
	sq.set(0, a, 3, 90, 2);
	int b[] = {60};
	sq.set(3, b, 1, 70, 1);
	ScaleMask major = kScales[1].mask;
	CHECK(sq.conform(major, 0));
	CHECK_EQ(int(sq.steps[0].count), 2); // C# joined D
	CHECK_EQ(int(sq.steps[0].notes[0]), 62);
	CHECK_EQ(int(sq.steps[0].notes[1]), 64);
	CHECK_EQ(int(sq.steps[0].velocity), 90);
	CHECK_EQ(int(sq.steps[0].gate), 2);
	CHECK_EQ(int(sq.steps[3].notes[0]), 60);
	CHECK(!sq.conform(major, 0)); // already in the scale
	CHECK(!sq.conform(0xFFF, 0));
}

TEST_CASE(seq_chord_snaps_pitch_and_transpose_to_the_scale) {
	Track t;
	t.seqMode = true;
	t.scale = 1; // major on C
	SeqStep s;
	s.notes[0] = 60;
	s.notes[1] = 64;
	s.count = 2;
	int out[kMaxPool];
	CHECK_EQ(t.seqChord(s, out, 1), 2); // C# -> D, F stays
	CHECK_EQ(out[0], 62);
	CHECK_EQ(out[1], 65);
	t.scale = 0; // chromatic: plain semitones
	t.seqChord(s, out, 1);
	CHECK_EQ(out[0], 61);
}

TEST_CASE(record_overdub_adds_to_the_notes_on_a_step) {
	StepSequence sq;
	SeqRecorder r;
	r.overdub = true;
	const double q = 0.25;
	// Live: a later pass adds to the step, keeping the loudest velocity and longest gate.
	r.liveOn(sq, 4, 1.0, 60, 80);
	r.liveOff(sq, 60, 1.5, q); // 2 steps
	r.liveOn(sq, 4, 5.0, 64, 60);
	r.liveOff(sq, 64, 5.1, q);
	CHECK_EQ(int(sq.steps[4].count), 2);
	CHECK_EQ(int(sq.steps[4].notes[0]), 60);
	CHECK_EQ(int(sq.steps[4].notes[1]), 64);
	CHECK_EQ(int(sq.steps[4].velocity), 80);
	CHECK_EQ(int(sq.steps[4].gate), 2);
	// A note already there stays once.
	r.liveOn(sq, 4, 9.0, 60, 100);
	r.liveOff(sq, 60, 9.1, q);
	CHECK_EQ(int(sq.steps[4].count), 2);
	CHECK_EQ(int(sq.steps[4].velocity), 80);
	// An empty step is written as usual.
	r.liveOn(sq, 7, 2.0, 70, 90);
	r.liveOff(sq, 70, 2.1, q);
	CHECK_EQ(int(sq.steps[7].count), 1);
	// Step record: the cursor step keeps its notes and gets the new ones.
	r.cursor = 4;
	r.stepOn(sq, 67, 120);
	r.stepOff(67, 16);
	CHECK_EQ(int(sq.steps[4].count), 3);
	CHECK_EQ(int(sq.steps[4].velocity), 120);
	CHECK_EQ(int(sq.steps[4].gate), 2);
	CHECK_EQ(r.cursor, 5);
}

TEST_CASE(record_ignores_a_note_already_held) {
	StepSequence sq;
	SeqRecorder r;
	r.liveOn(sq, 0, 0.0, 60, 100);
	r.liveOn(sq, 3, 0.75, 60, 100);
	CHECK(!sq.has(3));
	CHECK_EQ(r.held(), 1);
	r.release();
	CHECK_EQ(r.held(), 0);
}
