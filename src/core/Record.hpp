#pragma once
#include "core/Model.hpp"

namespace talea {

/** SEQ mode recording: turns played notes (a keyboard via V/Oct, Gate and Velocity) into steps
of a StepSequence.

Live, with the transport running: a note goes to the step nearest its start (quantized to the
step grid, so to the Division), held for its length rounded to whole steps (at least one). A
note starting on a step while an earlier note on that step is still held joins its chord;
otherwise it replaces what the step held, or with `overdub` on joins those notes too. The
chord's velocity is its loudest note's and its gate its longest note's.

Step, with the transport stopped: each chord played goes to the cursor step (the first note
replaces the step, or joins it with `overdub`; notes held with it join it), gate one step
(overdub keeps a longer one). When every note is released the cursor moves on one step,
wrapping at the track's Steps.

The recorder holds no reference to the sequence: each call names the one to write, so the
target can follow the UI (the edited cycle's own sequence in Cycle edit). Real-time safe. */
class SeqRecorder {
public:
	static constexpr int kMaxHeld = 16;
	/** Step recording's next step. */
	int cursor = 0;
	/** Notes played on a step that already has notes join them, instead of replacing them. */
	bool overdub = false;

	/** Live: `note` started at musical time `time` on step `pos` (the nearest one); steps are
	`stepQuarters` long. */
	void liveOn(StepSequence& sq, int pos, double time, int note, int velocity);
	/** Live: `note` ended at `time`. */
	void liveOff(StepSequence& sq, int note, double time, double stepQuarters);
	/** Step: `note` started (written at the cursor). */
	void stepOn(StepSequence& sq, int note, int velocity);
	/** Step: `note` ended; once nothing is held the cursor moves on, wrapping at `steps`. */
	void stepOff(int note, int steps);
	/** Forgets the held notes (REC off, the transport starting or stopping). */
	void release() {
		count_ = 0;
	}
	int held() const {
		return count_;
	}

private:
	struct Held {
		int note;
		int pos;      // the step it was written to
		double start; // live: when it started
	};
	Held held_[kMaxHeld];
	int count_ = 0;

	int find(int note) const;
	bool add(int note, int pos, double start);
	void remove(int i);
	bool chordHeldOn(int pos) const;
};

} // namespace talea
