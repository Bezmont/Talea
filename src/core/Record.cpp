#include "core/Record.hpp"

#include <algorithm>
#include <cmath>

namespace talea {

int SeqRecorder::find(int note) const {
	for (int i = 0; i < count_; i++) {
		if (held_[i].note == note)
			return i;
	}
	return -1;
}

bool SeqRecorder::add(int note, int pos, double start) {
	if (count_ >= kMaxHeld)
		return false;
	held_[count_++] = {note, pos, start};
	return true;
}

void SeqRecorder::remove(int i) {
	held_[i] = held_[--count_];
}

bool SeqRecorder::chordHeldOn(int pos) const {
	for (int i = 0; i < count_; i++) {
		if (held_[i].pos == pos)
			return true;
	}
	return false;
}

void SeqRecorder::liveOn(StepSequence& sq, int pos, double time, int note, int velocity) {
	note = std::clamp(note, 0, 127);
	if (pos < 0 || pos >= kMaxSteps || find(note) >= 0)
		return;
	if ((overdub || chordHeldOn(pos)) && sq.has(pos)) {
		// Played with the chord already on this step (or overdubbing what it holds): joins it,
		// the loudest note setting its velocity.
		if (sq.addNote(pos, note, velocity))
			sq.setVelocity(pos, std::max(int(sq.steps[pos].velocity), velocity));
	}
	else
		sq.set(pos, &note, 1, velocity, 1);
	add(note, pos, time);
}

void SeqRecorder::liveOff(StepSequence& sq, int note, double time, double stepQuarters) {
	int i = find(std::clamp(note, 0, 127));
	if (i < 0)
		return;
	Held h = held_[i];
	remove(i);
	if (!sq.has(h.pos) || !(stepQuarters > 0.0))
		return;
	int steps = std::max(1, int(std::lround((time - h.start) / stepQuarters)));
	// The chord's longest note sets the gate (a fresh chord starts at one step).
	sq.setGate(h.pos, std::max(int(sq.steps[h.pos].gate), steps));
}

void SeqRecorder::stepOn(StepSequence& sq, int note, int velocity) {
	note = std::clamp(note, 0, 127);
	if (cursor < 0 || cursor >= kMaxSteps || find(note) >= 0)
		return;
	if ((overdub || count_ > 0) && sq.has(cursor)) {
		if (sq.addNote(cursor, note, velocity))
			sq.setVelocity(cursor, std::max(int(sq.steps[cursor].velocity), velocity));
	}
	else
		sq.set(cursor, &note, 1, velocity, 1);
	add(note, cursor, 0.0);
}

void SeqRecorder::stepOff(int note, int steps) {
	int i = find(std::clamp(note, 0, 127));
	if (i < 0)
		return;
	remove(i);
	if (count_ == 0)
		cursor = (cursor + 1) % std::clamp(steps, 1, kMaxSteps);
}

} // namespace talea
