#pragma once
#include "core/Model.hpp"

#include <cstdint>
#include <limits>
#include <vector>

namespace talea {

/** Abstract note event (core rule 1): hosts turn these into voltages or MIDI. */
struct NoteEvent {
	double time; // musical time in quarter notes since reset
	int track;
	bool on;
	int note;     // MIDI note number (note-on: the chord's lowest)
	int velocity; // 0-127
	/** Note-on: every note of the chord (Poly), sorted, chord[0] == note; Mono: just `note`.
	The chord shares the track's gate: the next note-off ends all of it. */
	int chordSize = 1;
	uint8_t chord[kMaxPool] = {};
	/** CC track note-on: `velocity` is the value, reached `glide` quarter notes after `time`
	(0 = at once); `note` and the chord mean nothing. */
	bool cc = false;
	int ccNumber = 0;
	double glide = 0.0;
};

/** Fixed-capacity event buffer: no allocation on the audio thread. */
struct EventList {
	static constexpr int kCapacity = 1024;
	NoteEvent events[kCapacity];
	int size = 0;

	void clear() {
		size = 0;
	}
	void push(const NoteEvent& e) {
		if (size < kCapacity)
			events[size++] = e;
	}
	/** Stable sort by time; at equal times note-offs come before note-ons. */
	void sort();

	const NoteEvent* begin() const {
		return events;
	}
	const NoteEvent* end() const {
		return events + size;
	}
};

/** When committed mutes take effect: at once, or on the next beat, bar (4 quarters) or
pattern loop (longest Length; a bar if every Length is infinite). */
enum MuteTiming { MUTE_NOW, MUTE_BEAT, MUTE_BAR, MUTE_PATTERN, kNumMuteTimings };
inline constexpr const char* kMuteTimingNames[kNumMuteTimings] = {"Immediately", "Next beat", "Next bar", "End of pattern"};

/** Pattern slots in the bank, and entries in a pattern chain. */
constexpr int kNumPatterns = 16;
constexpr int kMaxChain = 16;

/** Song mode: a list of patterns, each played for a number of bars. */
constexpr int kMaxSongEntries = 64;
constexpr int kMaxSongBars = 999;
struct SongEntry {
	int slot = 0;
	int bars = 1;
	bool operator==(const SongEntry& o) const {
		return slot == o.slot && bars == o.bars;
	}
};
struct Song {
	SongEntry entries[kMaxSongEntries];
	int count = 0;
	bool loop = false;
	/** Bars of the whole song. */
	int totalBars() const;
	/** Inserts `e` before entry `at` (at == count appends); false when full. */
	bool insert(int at, SongEntry e);
	void remove(int at);
	bool operator==(const Song& o) const;
};

/** Each track's position is a pure function of musical time, so any window size gives the
same events (a DAW block or a single Rack sample). State carried between windows: which
notes are sounding and the repeat bursts in flight. A window that doesn't start where the
previous one ended (reset, seek) drops the bursts.

The bank holds kNumPatterns patterns; `pattern` is the one playing, and it is what gets
edited. A pattern switch restarts the tracks from step 1: track positions are a function of
the time since the playing pattern started (patternOrigin()), which is 0 until the first
switch. All times passed in and out are host musical time. */
class Engine {
public:
	Pattern pattern;
	/** Per-track host inputs, set before process(): Phrase CV in volts (+-5) for the
	CV-input Phrase, and transposition in semitones (scale-quantized). */
	double phraseCv[kNumTracks] = {};
	int transpose[kNumTracks] = {};
	/** Per-track CV offsets in parameter units (the IN expander), added on top of cycle
	overrides, step locks and random. Steps, Division and Length are ignored: they set the
	loop length (see randomisable()). */
	float mod[kNumTracks][kNumTrackParams] = {};

	/** Mute settings: when committed mutes land, and whether muting cuts the sounding note
	and pending repeats (true) or lets them finish (false). */
	int muteTiming = MUTE_NOW;
	bool muteCuts = false;

	/** Mutes or unmutes a track right away; a cut note ends at `time`. */
	void setMuted(int track, bool muted, double time, EventList& out);
	/** Staged mute changes: a bit per track to toggle, applied by commitMutes(). */
	void stageMute(int track) {
		staged_ ^= uint16_t(1u << track);
	}
	void clearStagedMutes() {
		staged_ = 0;
	}
	uint16_t stagedMutes() const {
		return staged_;
	}
	/** Schedules the staged changes for the first muteTiming boundary after `now` (the end
	of the last processed window). Changes already scheduled move with them. */
	void commitMutes(double now);
	/** Committed changes still waiting for their boundary, and that boundary. */
	uint16_t scheduledMutes() const {
		return scheduled_;
	}
	double scheduledMuteTime() const {
		return scheduledTime_;
	}
	/** Applies the scheduled changes if their boundary is at or before `time`. process()
	does this itself; a host calls it while stopped so "Immediately" still applies. */
	void applyDueMutes(double time, EventList& out);
	/** Drops staged and scheduled changes. */
	void clearMuteQueue() {
		staged_ = scheduled_ = 0;
	}

	/** Temp: while active, edits to `pattern` are a temporary overlay. Ending it reverts
	them, or keeps them; mutes are performance state and are never reverted. */
	void beginTemp() {
		if (!tempActive_) {
			tempSaved_ = pattern;
			tempActive_ = true;
		}
	}
	void endTemp(bool keep);
	bool tempActive() const {
		return tempActive_;
	}
	/** The pattern without the Temp overlay: what gets saved. */
	const Pattern& committedPattern() const {
		return tempActive_ ? tempSaved_ : pattern;
	}
	/** Forgets the overlay without touching `pattern` (after loading a new one). */
	void dropTemp() {
		tempActive_ = false;
	}

	/** When a queued pattern switch lands (a MuteTiming; "End of pattern" counts loops of the
	playing pattern from its start). */
	int patternQuantize = MUTE_PATTERN;

	int currentPattern() const {
		return current_;
	}
	/** Slot waiting to play, or -1, and the time it switches. */
	int queuedPattern() const {
		return queued_;
	}
	double patternSwitchTime() const {
		return switchTime_;
	}
	/** Musical time the playing pattern started. */
	double patternOrigin() const {
		return origin_;
	}
	/** Counts switches, so a host can see one happened (e.g. to reload its controls). */
	uint32_t patternSwitches() const {
		return switches_;
	}
	/** A slot's pattern (the playing one without its Temp overlay). */
	const Pattern& patternSlot(int slot) const {
		return slot == current_ ? committedPattern() : slots_[slot];
	}
	/** A slot holds something other than the default pattern. */
	bool patternUsed(int slot) const;
	/** Replaces a slot. The playing slot's pattern changes at once and Temp is dropped. */
	void storePattern(int slot, const Pattern& p);
	/** Puts the default pattern in a slot (without building one on the caller's stack). */
	void clearPattern(int slot);
	/** Makes `slot` the playing pattern without a switch (loading a saved bank). */
	void setCurrentPattern(int slot);
	/** Every slot back to the default pattern, slot 1 playing, nothing queued. The pattern
	start stays (rewind() moves it), so loading a bank while playing doesn't jump. */
	void resetBank();

	/** Queues `slot` for the next patternQuantize boundary after `now`; `immediate` (or
	"Immediately") switches at `now`. Clears the chain. Queueing the playing slot cancels a
	queued switch; switching to it immediately restarts it. Leaving a pattern ends Temp,
	reverting its edits; edits made to the pattern stay in its slot. */
	void queuePattern(int slot, double now, EventList& out, bool immediate = false);
	/** Adds `slot` to the chain. The first entry is queued like queuePattern(); once the
	chain plays, each pattern end moves to the next entry, wrapping round. */
	void chainPattern(int slot, double now, EventList& out, bool immediate = false);
	void clearChain();
	int chainLength() const {
		return chainLen_;
	}
	int chainEntry(int i) const {
		return chain_[i];
	}
	/** Chain entry playing, or -1 before the chain has started. */
	int chainPosition() const {
		return chainPos_;
	}
	/** Back to the start (host reset): the playing pattern restarts at time 0. A queued
	switch stays queued, timed from the new start. */
	void rewind();

	/** Song mode. While it plays, each entry plays its pattern for its bars (the pattern
	restarting from step 1 each time it ends), then the next entry starts on the bar line;
	after the last one it loops or ends (takeSongEnd()). A pattern switch that isn't the
	song's (a click, the chain) overrides it: the song remembers the bar it left, and
	resumeSong() picks it up there. Edit `song` on the audio thread, through the edit helpers
	below while it plays, so the playing position stays in step. */
	Song song;
	bool songOn() const {
		return songOn_;
	}
	/** On: the song starts from its first entry, at once at time <= 0, otherwise on the next
	bar line. Off: the playing pattern carries on. */
	void setSongOn(bool on, double now, EventList& out);
	/** Song on / off from a saved patch: nothing switches; Play starts the song. */
	void restoreSongOn(bool on) {
		songOn_ = on;
		songPos_ = -1;
		overridden_ = false;
		jumpPending_ = false;
	}
	/** From the first entry: at once at time <= 0 (after a reset), otherwise the next bar. */
	void startSong(double now, EventList& out);
	/** After an override: back to the bar the song left, on the next bar line. */
	void resumeSong(double now);
	/** Song entry playing, or -1 (not started, overridden, or waiting for its bar line). */
	int songEntry() const {
		return songDriving() ? songPos_ : -1;
	}
	/** Host time the playing entry's bar 1 began. */
	double songEntryStart() const {
		return entryStart_;
	}
	bool songOverridden() const {
		return songOn_ && overridden_;
	}
	/** Where the song resumes after an override: entry and bar (from 0). */
	int songResumeEntry() const {
		return resumeEntry_;
	}
	int songResumeBar() const {
		return resumeBar_;
	}
	/** A start or resume waiting for its bar line: the time, the entry and bar. */
	bool songPending() const {
		return songOn_ && jumpPending_;
	}
	double songPendingTime() const {
		return jumpTime_;
	}
	int songPendingEntry() const {
		return jumpEntry_;
	}
	int songPendingBar() const {
		return jumpBar_;
	}
	/** True once after the song ended (no Loop): the host stops and rewinds. */
	bool takeSongEnd() {
		bool e = songEnded_;
		songEnded_ = false;
		return e;
	}
	/** A new entry's bars: the slot's pattern length rounded up to whole bars. */
	int defaultBars(int slot) const;
	/** Song edits that keep the playing entry in step (`now`: the host time, for an entry
	that has to start on the next bar line). */
	void songInsert(int at, SongEntry e, double now);
	void songRemove(int at, double now);
	void songClear();
	/** Counts the song's restarts of the playing pattern (pattern ends, for the host). */
	uint32_t patternRestarts() const {
		return restarts_;
	}

	/** Emits the events in the window (from, to], in time order. */
	void process(double from, double to, EventList& out);
	/** Emits note-offs at `time` for every sounding note and drops pending repeats. */
	void allNotesOff(double time, EventList& out);

	/** What the engine carries from one window to the next: the sounding notes, the
	repeats still to come and where the last window ended. A host that computes ahead of
	playback saves it after each window; after an edit it loads the state saved where
	playback really is and computes the rest again with the new settings. Pattern
	switches, mutes and the song aren't part of it. */
	struct Playback;
	void savePlayback(Playback& p) const;
	void loadPlayback(const Playback& p);

	/** Per-track cycle that plays alone while it is being edited (the host's Cycle edit);
	-1 = all cycles play in turn. */
	int soloCycle[kNumTracks];

	Engine();

	/** The settings `track` plays with at host time `t`, for display: its playing cycle, the
	step's locks, random and CV, as a pulse on the current step would get them. */
	Track settingsAt(int track, double t) const;

	/** Cycle slot `track` plays at musical time `t` (its first cycle before time 0). */
	int cycleAt(int track, double t) const;
	/** Position of `track` in its loops at musical time `t` (loops completed since the pattern
	started plus the fraction through the current one; 0 before time 0). What Phrase and Groove
	follow when synced to the loop (Track::loopPosition). */
	double loopsAt(int track, double t) const;
	/** SEQ recording: the step of `track` nearest musical time `t` (quantized to the step grid
	of the cycle playing then; past the last step of a loop, the next loop's first), and that
	step's length in quarter notes. */
	int nearestStep(int track, double t, double& stepQuarters) const;
	/** Step index of `track` at musical time `t`, or -1 before time 0. */
	int stepAt(int track, double t) const;
	/** End-of-pattern period in quarter notes: the longest track period (the sum of its
	cycles' loops; a loop is the Length, or one pass of the Steps if Length is infinite). */
	double patternQuarters() const;

	/** Locks a track's random sequences where they are at host time `now` (see
	TrackRandom::locked). */
	void lockRandom(int track, double now);

	/** Rotated hit mask of a track, or of one of its cycles, cached between calls. In SEQ mode
	the steps of its sequence that have notes. */
	StepMask hits(int track);
	StepMask hits(int track, int cycle);

private:
	struct Voice {
		bool active = false;
		double offTime = 0.0;
		double onTime = std::numeric_limits<double>::quiet_NaN(); // start of the latest note
		int note = 0;
		long ordinal = 0;      // Style motion position (Choke mode)
		long ordinalLoop = -1; // cycle span the motion count belongs to
	};
	Voice voices_[kNumTracks];

	struct HitCache {
		int steps = -1, pulses = -1, rotate = -1;
		StepMask manual = 0, removed = 0;
		StepMask hits = 0;
	};
	HitCache cache_[kNumTracks][kMaxCycles];
	/** `t` is the track as cycle `cycle` plays it (-1 = the track itself), cached in `slot`. */
	StepMask hitsOf(int track, int slot, const Track& t, int cycle);

	/** A stretch of time one cycle plays: a track's spans run back to back from time 0,
	numbered from 0. */
	struct Span {
		long index;
		int cycle;
		double start, end; // end = INFINITY for a single cycle with an infinite Length
	};
	/** Cycle slots that play in turn (just the solo cycle while one is set). */
	int cycleList(int track, int* list) const;
	Span spanByIndex(int track, long index) const;
	Span spanAt(int track, double t) const;

	// Cycles' settings built for the track being processed.
	Track resolved_[kMaxCycles];
	bool resolvedValid_[kMaxCycles] = {};
	TrackRandom randomResolved_[kMaxCycles];
	bool randomValid_[kMaxCycles] = {}, randomActive_[kMaxCycles] = {};

	/** Repeats following one pulse. Times are computed from the live knob values as each
	repeat comes due, so turning Time or Pace affects bursts already playing. */
	struct Burst {
		bool live = false;
		double start = 0.0; // pulse time
		double prev = 0.0;  // time of the last note played (pulse or repeat)
		int fired = 0;      // repeats played so far
		uint64_t id = 0;    // the pulse's grid position, keys its repeats' Probability
		int cycle = 0;      // cycle slot whose settings the repeats play with
		double grid = 0.0;  // the pulse's grid time and loop count, for its random values
		long randomLoop = 0;
		int lockPos = -1; // step whose locks the repeats play with, -1 = none
		double loopStart = 0.0, loopLength = 1.0; // the pulse's loop (randomLoop counts it)
		int seqPos = -1; // SEQ mode: the sequence step it plays, -1 = none
	};
	static constexpr int kMaxBursts = 16; // per track, for Tail mode
	Burst bursts_[kNumTracks][kMaxBursts];
	double lastTo_ = std::numeric_limits<double>::quiet_NaN(); // end of the previous window

public:
	struct Playback {
		Voice voices[kNumTracks];
		Burst bursts[kNumTracks][kMaxBursts];
		double lastTo = std::numeric_limits<double>::quiet_NaN();
	};

private:

	uint16_t staged_ = 0, scheduled_ = 0;
	bool tempActive_ = false;
	Pattern tempSaved_;
	double scheduledTime_ = 0.0;

	// Pattern bank. slots_[current_] is stale while it plays: `pattern` is its live copy.
	std::vector<Pattern> slots_;
	uint16_t used_ = 0; // slots other than the playing one holding a non-default pattern
	int current_ = 0;
	int queued_ = -1;
	int queuedTiming_ = MUTE_PATTERN;
	int queuedChainPos_ = -1; // chain entry the queued switch plays, -1 = not from the chain
	double switchTime_ = 0.0;
	double origin_ = 0.0;
	uint32_t switches_ = 0;
	int chain_[kMaxChain] = {};
	int chainLen_ = 0;
	int chainPos_ = -1;

	// Song playback.
	bool songOn_ = false;
	int songPos_ = -1;        // entry playing, -1 = not started
	double entryStart_ = 0.0; // host time of the entry's bar 1
	bool overridden_ = false;
	int resumeEntry_ = 0, resumeBar_ = 0;
	bool jumpPending_ = false; // a start / resume waiting for jumpTime_
	double jumpTime_ = 0.0;
	int jumpEntry_ = 0, jumpBar_ = 0;
	bool songEnded_ = false;
	uint32_t restarts_ = 0;
	/** The song drives the patterns (playing, not overridden). */
	bool songDriving() const {
		return songOn_ && !overridden_ && !jumpPending_ && songPos_ >= 0 && songPos_ < song.count;
	}
	/** Next song boundary after `from` (entry end, pattern restart or pending jump), or
	INFINITY. */
	double nextSongEvent(double from) const;
	/** Handles the song boundary at `at` (sounding notes end at `cut`); false when the song
	ended. */
	bool songEvent(double at, double cut, EventList& out);
	/** Plays entry `e` from bar `bar`, the switch landing at `at`. */
	void startEntry(int e, int bar, double at, double cut, EventList& out);
	/** A switch that isn't the song's, landing at `at`: the song steps aside. */
	void songOverride(double at);

	/** First `timing` boundary strictly after `now`, counted from the pattern's start. */
	double nextBoundary(int timing, double now) const;
	void queue(int slot, int timing, int chainPos, double now, EventList& out);
	/** Switches to `slot`, its tracks starting at `origin`; sounding notes end at `cut`. */
	void switchTo(int slot, double origin, double cut, EventList& out);
	void scheduleChainNext();
	/** Files the playing pattern in its slot and plays `slot` (no timing changes). */
	void makeCurrent(int slot);
	void processTracks(double from, double to, EventList& out);
	void clearBursts();
	void processTrack(int track, double from, double to, EventList& out);
};

} // namespace talea
