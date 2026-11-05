#pragma once
#include "core/Euclid.hpp"
#include "core/Params.hpp"
#include "core/Tonal.hpp"

namespace talea {

constexpr int kNumTracks = 16;
constexpr int kBaseNote = 60; // MIDI C4 = 0 V
constexpr int kInfiniteRepeats = -1;
/** Ramp is spread over this many repeats when Repeats is infinite. */
constexpr int kInfiniteRampRepeats = 16;

/** Accent shapes. The percussion ones are one-bar patterns on a 1/16 grid; the waveforms
span the Groove length. */
enum Groove { GROOVE_AGOGO, GROOVE_TIMBALES, GROOVE_CONGAS, GROOVE_BONGO, GROOVE_SAW, GROOVE_TRIANGLE, GROOVE_SINE, GROOVE_PULSE, kNumGrooves };
inline constexpr const char* kGrooveNames[kNumGrooves] = {"Agogo", "Timbales", "Congas", "Bongo", "Saw", "Triangle", "Sine", "Pulse"};
inline constexpr int kGrooveBars[] = {1, 2, 4, 8, 16, 32, 64};
inline constexpr int kDefaultGrooveLength = 2; // 4 bars
/** Timing (swing) subdivision: which steps count as "every 2nd". */
inline constexpr int kSwingSubdivisions[] = {1, 2, 4};
/** Accent at 100 % moves velocity this far above or below Velocity at the shape's peaks. */
constexpr int kAccentDepth = 63;

/** Notes in a track's pool (T-1: up to 8 pitches per track). */
constexpr int kMaxPool = 8;
/** CC tracks: MIDI CC numbers 0-119 (120-127 are channel mode messages), default Mod wheel. */
constexpr int kNumCcNumbers = 120;
constexpr int kDefaultCc = 1;
/** Cycles per track (T-1: 16; 8 here leaves room for a lane per cycle on the display). */
constexpr int kMaxCycles = 8;

/** A parameter value clamped to its range and snapped where the parameter snaps. */
float clampParam(TrackParam p, float v);

struct Track {
	float values[kNumTrackParams];
	/** Manually added hits, in unrotated step positions (Rotate shifts them too). Sticky:
	they stay when Pulses changes. */
	StepMask manualHits = 0;
	/** Generated hits switched off from the pads, in unrotated step positions. Sticky like
	manualHits. */
	StepMask removedHits = 0;
	/** Repeat mode: false = Choke (a new pulse cuts the previous repeats), true = Tail
	(repeats of successive pulses overlap). */
	bool repeatTail = false;
	/** GROOVE page selections: Groove shape, index into kGrooveBars, index into
	kSwingSubdivisions. */
	int groove = GROOVE_AGOGO;
	int grooveLength = kDefaultGrooveLength;
	int swingSubdivision = 0;

	/** Note pool, MIDI notes sorted ascending, at least one. Entered in KEYS mode. */
	int pool[kMaxPool] = {kBaseNote};
	int poolSize = 1;
	/** SCALE page: index into kScales, root pitch class (0 = C), the User scale's mask. */
	int scale = 0;
	int root = 0;
	ScaleMask userScale = kDefaultUserScale;
	/** STYLE page: Style, Mono (arpeggiate) vs Poly (chord), voicing direction. */
	int style = STYLE_FIXED;
	bool mono = false;
	bool voicingDown = false;
	/** PHRASE page: Phrase shape, index into kGrooveBars. */
	int phrase = PHRASE_CADENCE_1;
	int phraseLength = kDefaultGrooveLength;
	/** A muted track starts no new notes (MUTE mode). */
	bool muted = false;
	/** Track type (SETUP page): a CC track plays values instead of notes. Each pulse's
	Velocity (0-127, with locks, random, CV, Accent, Ramp) is the value, Sustain glides to
	it (0 = jump, 100 % = across the whole gap), and the pitch settings are unused. The
	host sends it as a CV lane and as MIDI CC `ccNumber`. */
	bool cc = false;
	int ccNumber = kDefaultCc;

	Track() {
		reset();
	}

	void reset();

	float get(TrackParam p) const {
		return values[p];
	}
	int getInt(TrackParam p) const {
		return int(values[p]);
	}
	/** Clamps to the parameter's range and snaps where the parameter snaps. */
	void set(TrackParam p, float v);

	int steps() const;
	/** Pulses limited to Steps. */
	int pulses() const;
	/** Generated hits minus removed hits plus manual hits, rotated, in played step
	positions. */
	StepMask hits() const;
	/** Pad edit on a played step position: turns that one step off or on without moving
	any other hit. */
	void toggleStep(int step);
	/** Unrotated position of played step `step` (where manual hits and locks are kept). */
	int unrotated(int step) const;

	/** Repeats after each pulse; kInfiniteRepeats at the knob's last position. */
	int repeats() const;
	/** Quarter notes from repeat i to repeat i + 1 (i = 0 is the pulse itself): Time shaped
	by Pace. */
	double repeatInterval(int i) const;
	/** Velocity of repeat i (1-based) of a burst, after Ramp. */
	int repeatVelocity(int i) const;
	/** Gate length in quarter notes for a note followed by a gap of `interval`. */
	double gateQuarters(double interval) const;
	/** CC track: glide time in quarter notes to a value followed by a gap of `interval`. */
	double glideQuarters(double interval) const;
	/** Lowest velocity Ramp and Accent reach: a CC value can be 0, a note keeps sounding. */
	int minVelocity() const {
		return cc ? 0 : 1;
	}

	/** Chance (0-1) that a note is skipped. */
	double skipChance() const;
	/** Probability CW: skipping also applies to each repeat. CCW: pulses only. */
	bool skipsRepeats() const;
	/** Groove shape value (-1..1) at musical time `time`. */
	double grooveValue(double time) const;
	/** `velocity` with Accent applied for a note at `time`. */
	int accented(int velocity, double time) const;
	/** Quarter notes step `k` of a loop plays away from the grid (Timing + Delay). */
	double stepShift(long k) const;
	/** Largest |stepShift()| at the current settings. */
	double maxStepShift() const;

	/** Step length in quarter notes. */
	double stepQuarters() const;
	/** Loop length in quarter notes; 0 = infinite. */
	double lengthQuarters() const;
	/** Time before the track repeats: Length, or one pass of the steps if Length is
	infinite. */
	double loopQuarters() const;
	/** Steps that play before Length restarts the track (all of them if it never cuts). */
	int playedSteps() const;

	ScaleMask scaleMask() const;
	bool inPool(int note) const;
	/** Adds a note to the pool, or removes it if present. The last note can't be removed and
	the pool holds at most kMaxPool. Returns whether the pool changed. */
	bool togglePoolNote(int note);
	/** One Harmony detent: CW (dir > 0) moves the lowest pool note up one scale step, CCW
	moves the highest down, skipping notes already in the pool. */
	void harmony(int dir);
	/** The note played as the `ordinal`-th note of the Style motion at musical time `time`:
	pool + Pitch, voiced by Voicing/Style (Mono: arpeggiated, Poly: lowest chord note),
	plus Range x Phrase and `transpose` semitones, quantized to the scale. */
	int noteFor(long ordinal, double time, double phraseCv = 0.0, int transpose = 0) const;
	/** The notes sounding for the `ordinal`-th note: Poly = the whole voiced chord, Mono = the
	one arpeggiated note. Same pitch path as noteFor(); sorted ascending, duplicates (notes
	the scale folds together) removed, so out[0] == noteFor(). Returns the count (>= 1). */
	int chordFor(long ordinal, double time, int* out, double phraseCv = 0.0, int transpose = 0) const;
};

/** Steps, Pulses, Rotate, Division and Length shape the whole loop, so a step can't lock them. */
bool lockable(TrackParam p);

/** A step's own note pool (a step lock of the pool): 1..kMaxPool MIDI notes, sorted and
unique; size 0 = none. It replaces the track's (or cycle's) pool for that step. */
struct PoolLock {
	uint8_t notes[kMaxPool] = {};
	uint8_t size = 0;

	/** Sets the pool from `n` notes (sorted, duplicates dropped, clamped to 0-127, at most
	kMaxPool); n = 0 clears it. */
	void set(const int* in, int n);
	void apply(Track& t) const;
	bool operator==(const PoolLock& o) const;
};

/** A cycle's own step locks (CYC EDIT), over the track's: a short list of (step, parameter,
value), since most cycles lock few steps, plus a short list of step pools. Steps are
unrotated positions like the track's. */
struct CycleLocks {
	static constexpr int kMax = 128;
	static constexpr int kMaxPools = 32;
	uint8_t step[kMax] = {};
	uint8_t param[kMax] = {};
	float value[kMax] = {};
	int count = 0;
	uint8_t poolStep[kMaxPools] = {};
	PoolLock pool[kMaxPools];
	int poolCount = 0;
	StepMask steps = 0; // steps with any lock
	uint32_t used = 0;  // parameters locked on any step

	bool empty() const {
		return count == 0 && poolCount == 0;
	}
	/** Step `pos`'s pool lock, or nullptr. */
	const PoolLock* poolAt(int pos) const;
	/** Locks step `pos`'s pool (n = 0 clears it); false if the list is full. */
	bool setPool(int pos, const int* notes, int n);

	bool any(int pos) const {
		return pos >= 0 && pos < kMaxSteps && (steps & (StepMask(1) << pos));
	}
	bool has(int pos, TrackParam p) const {
		return find(pos, p) >= 0;
	}
	float get(int pos, TrackParam p) const {
		int i = find(pos, p);
		return i >= 0 ? value[i] : 0.f;
	}
	/** Locks `p` on step `pos` (clamped and snapped); false if the list is full. */
	bool set(int pos, TrackParam p, float v);
	void clear(int pos, TrackParam p);
	void clearStep(int pos);
	void reset() {
		count = 0;
		poolCount = 0;
		steps = 0;
		used = 0;
	}
	void apply(Track& t, int pos) const;

private:
	int find(int pos, TrackParam p) const;
	int findPool(int pos) const;
	void update();
};

/** One cycle's differences from its track: a sparse set of parameter overrides (bit p of
`overridden` set = values[p] replaces the track's value), random amount overrides (likewise
in `randomOverridden` / randomAmounts), optionally its own step edits (replacing the track's
manual and removed hits), its own step locks (over the track's), optionally its own scale,
root, user scale and pool, and a mute. */
struct CycleOverrides {
	uint32_t overridden = 0;
	float values[kNumTrackParams] = {};
	uint32_t randomOverridden = 0;
	float randomAmounts[kNumTrackParams] = {};
	bool ownHits = false;
	StepMask manualHits = 0, removedHits = 0;
	CycleLocks locks;
	bool ownTonal = false;
	Track tonal; // its scale, root, userScale and pool when ownTonal (other fields unused)
	bool muted = false;

	bool has(TrackParam p) const {
		return overridden & (1u << p);
	}
	bool hasRandom(TrackParam p) const {
		return randomOverridden & (1u << p);
	}
	/** Plays differently from the track (ignoring the mute). */
	bool modified() const {
		return overridden || randomOverridden || ownHits || !locks.empty() || ownTonal;
	}
	bool empty() const {
		return !modified() && !muted;
	}
	/** Drops the parameter overrides and step edits, keeping the mute. */
	void clearChanges() {
		overridden = 0;
		randomOverridden = 0;
		ownHits = false;
		manualHits = removedHits = 0;
		locks.reset();
		ownTonal = false;
	}
};

struct TrackRandom;

/** A track's cycles. The track advances to the next cycle each time it completes a loop
(its Length, or one pass of its Steps if Length is infinite). */
struct TrackCycles {
	int count = 1;
	CycleOverrides cycles[kMaxCycles];

	void reset();
	/** The value parameter `p` plays with in cycle `c`. */
	float value(const Track& base, int c, TrackParam p) const;
	/** Overrides `p` in cycle `c` (clamped and snapped). */
	void set(int c, TrackParam p, float v);
	void clear(int c, TrackParam p);
	/** Drops every override and the mute of cycle `c`. */
	void clearCycle(int c);
	/** Step edit in cycle `c` (see Track::toggleStep). The cycle's first edit starts from
	the track's hits; from then on its hits are its own. */
	void toggleStep(const Track& base, int c, int step);
	/** Moves every override of `p` by `delta` (a relative edit of all cycles). */
	void shift(TrackParam p, float delta);
	/** Cycle `c`'s scale / root / pool for an edit: its own, copied from `base` on first use. */
	Track& tonalEdit(const Track& base, int c);
	/** Gives cycle `c` the scale, root, user scale and pool of `t`. */
	void setTonal(int c, const Track& t);
	/** `base` with cycle `c`'s overrides applied. */
	Track resolve(const Track& base, int c) const;
	/** Random amount of `p` in cycle `c`: its override, else the track's. Absolute: track
	amount edits don't move the overrides. */
	float randomAmount(const TrackRandom& rnd, int c, TrackParam p) const;
	void setRandom(int c, TrackParam p, float amount);
	void clearRandom(int c, TrackParam p);
	/** The track's random settings with cycle `c`'s amount overrides applied. */
	TrackRandom resolveRandom(const TrackRandom& rnd, int c) const;
	/** Loop length of cycle `c` in quarter notes (see Track::loopQuarters). */
	double loopQuarters(const Track& base, int c) const;
};

/** Steps, Division and Length set how long a loop is, so they can't be randomised: a random
loop length would stop tracks lining up and leave the pattern without a fixed length. */
bool randomisable(TrackParam p);

/** Values in a random sequence before it comes round again. */
constexpr int kRandomSequence = 16;
/** Parameters advancing per cycle loop by default (the rest per Random Rate tick). */
constexpr uint32_t kDefaultPerCycle = 1u << TP_PULSES | 1u << TP_ROTATE | 1u << TP_REPEATS | 1u << TP_REPEAT_TIME;

/** A track's randomisation (Rnd assign / RANDOM page). Per parameter: a bipolar amount
(-100..100 %; CW pushes the value up, CCW down, by up to the amount x its range) and
whether it moves on each Random Rate tick or each cycle loop. Each parameter draws from a
fixed 16-value sequence; Variance is the chance that a value is re-rolled each time the
sequence comes round. Draws are a pure function of the position, so a replay or seek plays
the same values. */
struct TrackRandom {
	float amount[kNumTrackParams] = {};
	uint32_t perCycle = kDefaultPerCycle;
	float variance = 0.f; // 0-100 %
	int rate = kDefaultRandomRate;
	uint32_t seed = 0;
	/** Shifts every sequence by this many values. */
	int phase = 0;
	/** Shifts Probability's skip pattern by this many steps. */
	int probPhase = 0;
	/** 0-100 %: Random Rate values glide to the next one over the end of each tick. */
	float slew = 0.f;
	/** Lock: the sequences stop changing. Each keeps the pass in force at the lock positions
	(a cycle loop and a Random Rate tick count), so Variance no longer re-rolls values. */
	bool locked = false;
	long lockLoop = 0, lockTick = 0;

	void reset(int track);
	bool active() const;
	bool isPerCycle(TrackParam p) const {
		return perCycle & (1u << p);
	}
	/** Value in [0, 1) at position `u` of parameter p's sequence (a loop or tick count). */
	double draw(TrackParam p, long u) const;
	/** Sequence position for loop / tick count `u` after Phase and Lock. */
	long position(TrackParam p, long u) const;
	/** A new seed: a fresh set of sequences. */
	void regenerate();
	/** Applies the random offsets to `t` for a note in loop `loop` (counted per cycle loop)
	at musical time `time`. */
	void apply(Track& t, long loop, double time) const;
};

/** A track's step locks: per step (unrotated position, so they move with Rotate like manual
hits), a sparse set of values that replace the track's (and its cycles') on that step's pulse
and repeats, and optionally the step's own note pool. Random applies on top. */
struct StepLocks {
	uint32_t mask[kMaxSteps] = {};
	float values[kMaxSteps][kNumTrackParams] = {};
	PoolLock pool[kMaxSteps];
	uint32_t used = 0; // parameters locked on any step
	StepMask pools = 0; // steps with a pool lock

	bool has(int pos, TrackParam p) const {
		return mask[pos] & (1u << p);
	}
	bool any(int pos) const {
		return mask[pos] != 0 || pool[pos].size;
	}
	/** Step `pos`'s pool lock, or nullptr. */
	const PoolLock* poolAt(int pos) const {
		return pos >= 0 && pos < kMaxSteps && pool[pos].size ? &pool[pos] : nullptr;
	}
	/** Locks step `pos`'s pool (n = 0 clears it). */
	void setPool(int pos, const int* notes, int n);
	/** Locks `p` on step `pos` (clamped and snapped); ignores parameters that can't lock. */
	void set(int pos, TrackParam p, float v);
	void clear(int pos, TrackParam p);
	void clearStep(int pos);
	/** Step `pos` takes the locks `mask` / `values` and pool `pool` (one step's worth). */
	void copyStep(int pos, uint32_t mask, const float* values, const PoolLock& pool);
	void reset() {
		*this = StepLocks();
	}
	/** Puts step `pos`'s locked values into `t`. */
	void apply(Track& t, int pos) const;

private:
	void updateUsed();
};

struct Pattern {
	Track tracks[kNumTracks];
	TrackCycles cycles[kNumTracks];
	TrackRandom random[kNumTracks];
	StepLocks locks[kNumTracks];

	Pattern() {
		reset();
	}

	void reset() {
		for (Track& t : tracks)
			t.reset();
		for (TrackCycles& c : cycles)
			c.reset();
		for (int i = 0; i < kNumTracks; i++)
			random[i].reset(i);
		for (StepLocks& l : locks)
			l.reset();
	}

	/** Sets a track's own value of `p`; overrides in its cycles move by the same amount. */
	void setBase(int track, TrackParam p, float v);
	/** The track differs from a default one (values, steps, pool, cycles, random, locks,
	mute). */
	bool trackUsed(int track) const;
};

/** Same musical content: what plays and what is saved (unused cycle override values and pool
entries past poolSize don't count). */
bool operator==(const Pattern& a, const Pattern& b);
inline bool operator!=(const Pattern& a, const Pattern& b) {
	return !(a == b);
}

/** Each track's note pool as it stood before the current run of Harmony edits, so the knob's
menu can put it back.

Harmony is one-way: a CW detent moves the lowest pool note up, a CCW detent the highest down,
so turning back lands on a different pool than it started from, and alternating directions
contracts the pool towards a cluster. This remembers the way back.

A run continues while Harmony is the only thing changing the pool. If anything else edits it,
or the edit target changes (a track's own pool vs. a cycle's), the next detent starts a new
run from the pool as it then stands, so no other edit has to notify this.

Session state, held by the module and not saved with the patch: it is the knob's way back
during an edit, not part of the pattern. */
struct HarmonyUndo {
	/** Call before applying detents to `t`, the pool of `track` in `slot` (-1 = the track's
	own pool, >= 0 = that cycle's). Starts a new run when the pool or the slot moved on. */
	void before(int track, int slot, const Track& t);
	/** Call after applying them, so the next detent is recognised as the same run. */
	void after(int track, const Track& t);
	/** Whether `slot` of `track` has a pool to go back to. No side effects, so a caller can
	ask before resolving a cycle's own pool, which would otherwise create one. */
	bool has(int track, int slot) const;
	/** Puts the pool back and ends the run; false if there was nothing to restore. */
	bool restore(int track, int slot, Track& t);

private:
	PoolLock base[kNumTracks]; // the pool the run started from
	PoolLock last[kNumTracks]; // the pool as Harmony left it
	int slotOf[kNumTracks] = {};
	bool held[kNumTracks] = {};
};

} // namespace talea
