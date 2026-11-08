#pragma once
// Every tooltip text of Talea and its expanders, in one place: the panel elements' descriptions
// (Rack shows them under the element's name on hover) and the context display's hover tips.
// This file is the reference for the user documentation too. State-dependent notes are built
// in Talea.cpp (Talea::paramNote / padLabel / padDescription, ContextDisplay::hoverText) from
// the strings here and a few short inline ones.

#include "core/Params.hpp"

#include <cstring>
#include <string>

namespace tips {

// ---------------------------------------------------------------- Talea: transport
constexpr const char* kPlay =
	"Starts and pauses the transport. Play resumes where it stopped; Reset goes back to step 1. "
	"Pausing releases all gates.";
constexpr const char* kReset =
	"Sends every track back to step 1. Kill stuck notes is in the module's right-click menu.";
constexpr const char* kTap = "Tap in time to set the tempo (internal clock only).";
constexpr const char* kTempo = "Internal clock tempo. Ignored while CLOCK IN is patched.";
constexpr const char* kClockLight = "Blinks on each quarter note.";
constexpr const char* kClockIn =
	"External clock (triggers). Pulses per quarter note: context menu (default 4). "
	"Patched, it replaces the Tempo knob.";
constexpr const char* kResetIn = "Trigger: every track back to step 1.";
constexpr const char* kRunIn =
	"Gate: plays while high. With Run input = trigger (context menu), each trigger toggles play.";
constexpr const char* kRec =
	"SEQ mode: records NOTE IN into the selected track's sequence (the edited cycle's in CYC EDIT). "
	"Playing: live, each note to the nearest step. Stopped: step record at the blinking pad. "
	"Light solid = live, blinking = step record.";
constexpr const char* kOverdub =
	"Recording only. On: notes played on a step that already has notes are added to them. "
	"Off (default): they replace them.";
constexpr const char* kRecPitch =
	"Poly V/oct from a keyboard, e.g. VCV MIDI > CV with Polyphony channels set to 8. "
	"Channel n goes with gate channel n. In SEQ mode the keys also play on the selected track.";
constexpr const char* kRecGate = "Poly gate, one channel per held key. Keys held together form a chord.";
constexpr const char* kRecVelocity = "Poly velocity, 0-10 V. Unpatched: notes take the Velocity knob's value.";

// ---------------------------------------------------------------- Talea: perform
constexpr const char* kTemp =
	"While on, every pattern edit is temporary. Click again: discard (revert). Ctrl+click: keep. "
	"Right-click: Keep / Discard. Mutes are never reverted.";
constexpr const char* kAll = "While on, knob turns move every track by the same amount (header: ALL).";
constexpr const char* kRndAssign =
	"While on, turning a knob sets that parameter's random amount instead of its value "
	"(centre = none, CW = up to +100 %, CCW = down to -100 %). Clear amounts on the RANDOM page.";

// ---------------------------------------------------------------- Talea: knobs (by TrackParam)
constexpr const char* kTrackParam[talea::kNumTrackParams] = {
	/* Steps */ "Number of steps in the track's loop (1-64).",
	/* Pulses */ "Euclidean hits, spread as evenly as possible over the steps. 0 = silent.",
	/* Rotate */ "Shifts the whole pattern along the steps, manual hits included.",
	/* Division */ "Length of each step, as a note value (triplets and free included).",
	/* Length */
	"Restarts the track after this long, whatever Steps is. Infinite = Steps alone sets the loop. "
	"Steps it cuts off are drawn faint.",
	/* Repeats */ "Extra triggers after each pulse. Max = until the next pulse. Choke / Tail on the REPEAT page.",
	/* Ramp */ "Velocity across the repeats. CW: builds up to Velocity. CCW: fades down from it.",
	/* Repeat time */ "Spacing between repeats, as a note value.",
	/* Pace */ "CW: repeats slow down. CCW: they speed up. Centre = even.",
	/* Velocity */ "Base velocity of the track's notes (0-127).",
	/* Probability */
	"Chance notes play. CW: any note can skip, repeats included. CCW: pulses only (with their repeats). "
	"The same notes skip on every pass.",
	/* Sustain */ "Gate length: 50 % = one step, 100 % = two (notes overlap and retrigger).",
	/* Accent */ "Velocity variation around Velocity, shaped by the Groove chosen on the GROOVE page.",
	/* Timing */ "Swing: every 2nd step later (CW) or earlier (CCW). Subdivision on the GROOVE page.",
	/* Delay */ "Moves the whole track later (CW) or earlier (CCW), up to one step.",
	/* Pitch */ "Transposes the note pool in semitones (up to 4 octaves each way), snapped to the scale.",
	/* Voicing */
	"How far the Style moves the chord: each position takes the lowest note up an octave "
	"(or the highest down). Style on the STYLE page.",
	/* Range */
	"Adds up to this many semitones above the pool, following the Phrase over its length. "
	"Phrase on the PHRASE page.",
};
// Shorter tooltip names than the core's (which carry hints the descriptions now give).
constexpr const char* kProbabilityName = "Probability";
constexpr const char* kTimingName = "Timing (swing)";

constexpr const char* kHarmony =
	"Endless. Each CW detent moves the lowest pool note up a scale step, each CCW detent the "
	"highest down one. Right-click: Restore pool.";
constexpr const char* kVariance =
	"Chance each of the track's 16 random values is re-rolled when the sequence comes round "
	"(0 = the same values forever, 100 % = new every pass).";
constexpr const char* kRandomRate = "How fast the track's random values advance.";

// ---------------------------------------------------------------- Talea: pad section
constexpr const char* kModeStep =
	"Pads = 16 steps of the selected track (see STEP PAGE). Click toggles a pulse; Ctrl+click "
	"selects steps for locks. SEQ mode: click selects a step, right-click makes it a rest. "
	"STEP again ends a selection.";
constexpr const char* kModeCycle = "Pads 1-8 = cycle slots. Click selects (past the last adds cycles); Ctrl+click mutes.";
constexpr const char* kModeMute =
	"Pads = tracks. Click stages a mute change, MUTE again applies them; Ctrl+click mutes at once. "
	"Right-click: timing and options.";
constexpr const char* kModePattern =
	"Pads = the 16 pattern slots. Click queues a switch, Shift+click builds a chain, Ctrl+click "
	"switches at once. Right-click: switch timing, clear chain.";
constexpr const char* kCycleEdit =
	"On: knob turns override the parameter in the selected cycle, and only that cycle plays. "
	"Off: knobs move the track and every cycle's override with it.";
constexpr const char* kCopy =
	"Arms copy: the next click copies its target (track button, step / cycle / pattern pad, or "
	"knob). Click again to disarm.";
constexpr const char* kPaste =
	"Arms paste (needs a clipboard): every matching target clicked receives it, until PASTE is "
	"clicked again.";
constexpr const char* kClear =
	"Arms clear: the next click resets its target (track button, step / cycle / pattern pad, or "
	"knob). Ctrl+click: stays armed until clicked again.";
constexpr const char* kStepPage =
	"STEP mode: shows these 16 steps on the pads (turns Follow off). Blinks while the playhead "
	"is on it and another page is shown.";
constexpr const char* kFollow = "Pads and the STEP page follow the playhead's step page.";
constexpr const char* kTrackSelect =
	"Click: select the track (any pad mode). Ctrl+click: add to / remove from the group. "
	"Shift+click: group the range from the selected track. Grouped tracks move together on knob turns.";

// ---------------------------------------------------------------- Talea: outputs
constexpr const char* kPitchOut =
	"Poly, channel n = track n: each track's note in V/oct (a chord's lowest note; full chords "
	"on the OUT expander).";
constexpr const char* kGateOut = "Poly, channel n = track n. Repeats included.";
constexpr const char* kVelocityOut = "Poly, channel n = track n. 0-10 V (127 = 10 V), accent and ramp included.";
constexpr const char* kClockOut = "Clock pulses while playing, at the PPQN set in the context menu.";
constexpr const char* kResetOut = "Trigger on each reset.";
constexpr const char* kEopOut =
	"Trigger at the end of each pattern loop (the longest track's period) and on each pattern switch.";

// ---------------------------------------------------------------- IN expander
constexpr const char* kInAmount = "Attenuverter for this CV input (default 100 %).";
constexpr const char* kInCv =
	"Poly, channel n = track n (mono = every track). +-5 V x amount = +- the full range, added "
	"on top of the knob, locks and random.";
constexpr const char* kInTranspose = "Poly V/oct, channel n = track n (mono = every track). Transposes the notes.";
constexpr const char* kInPhrase =
	"Poly, channel n = track n. Drives the phrase of tracks whose Phrase is set to CV "
	"(-5..+5 V = bottom..top).";
constexpr const char* kInPattern = "0-10 V selects pattern 1-16.";
constexpr const char* kInTemp = "Temp is on while high.";
constexpr const char* kInMute = "Poly, channel n = track n: high = muted. A patched track follows its gate at each change.";
constexpr const char* kInMuteApply = "Trigger: applies the staged mutes.";
constexpr const char* kInRegen = "Trigger: new random values for the track (channel n = track n; mono = every track).";
constexpr const char* kInLink = "Green while attached: place IN directly left of Talea.";

// ---------------------------------------------------------------- OUT expander
constexpr const char* kOutPitch =
	"Poly, a fixed channel count (Polyphony in the context menu, 8 by default): one voice per "
	"chord note; a voice holds its pitch after its gate falls. Mono style: the sounding note. "
	"CC track: the value, 0-10 V, on channel 1.";
constexpr const char* kOutGate = "One gate per voice, on the same channels as the pitch; unused voices stay low.";
constexpr const char* kOutVelocity = "Per voice, 0-10 V, held after its gate.";
constexpr const char* kOutTrackLight = "Dim orange = idle, white = gate, blue-grey = muted; off when not attached.";
constexpr const char* kOutLink = "Green while attached: place OUT directly right of Talea.";

// ---------------------------------------------------------------- context display: page tabs
// In tab order: TRACK STEP CYCLE KEYS SCALE PHRASE STYLE REPEAT GROOVE RANDOM PATTERN SONG SETUP.
constexpr const char* kPage[] = {
	"The selected track's shape, the clock and tempo, and its steps (a lane per cycle with several).",
	"The track's steps. With locks (or in SEQ mode) a lane per locked parameter: click / drag to edit.",
	"The track's cycles: how many, which one is edited, mute, and clearing a cycle's changes.",
	"Keyboard: the track's note pool, selected steps' own pool, or in SEQ mode the selected step's notes.",
	"The scale and root notes snap to, and the scale's notes.",
	"The contour Range follows over time: its shape, length and sync.",
	"How Voicing moves the chord over successive notes; poly chords or mono arpeggio.",
	"The repeats of a pulse: Choke or Tail.",
	"The Accent shape, its length and sync, and the swing subdivision.",
	"Random amounts per parameter, the random sequence, and clearing amounts.",
	"Pattern slots: play, copy, clear; switch timing.",
	"A song: patterns in order, each for a number of bars.",
	"The track's MIDI channel, type (note / CC) and mode (EUCLID / SEQ); MIDI clock.",
};

// ---------------------------------------------------------------- context display: option rows
// Keyed by page (tab name, or MUTE for the mute view) and the row's label.
struct RowTip {
	const char* page;
	const char* label;
	const char* text;
};
constexpr RowTip kRows[] = {
	{"MUTE", "APPLY", "When staged mute changes apply: at once, on the next beat, bar or pattern end."},
	{"MUTE", "ON MUTE", "FINISH: a sounding note and its repeats play out. CUT: they stop at once."},

	{"CYCLE", "COUNT", "How many cycles the track has. Each plays one pass of the track's loop, in turn."},
	{"CYCLE", "CYCLE", "The cycle CYC EDIT and this page work on. * = has changes of its own, (n) = muted."},
	{"CYCLE", "MUTE", "Mutes the selected cycle: the track is silent while it plays."},
	{"CYCLE", "CLEAR",
	 "Drops the selected cycle's own changes: ALL, or one item (HITS = step edits, LOCKS, SCALE, SEQ, MODE, "
	 "a parameter, R.x = a random amount)."},

	{"PATTERN", "PLAY", "Queues a pattern slot (it switches at the SWITCH timing). * = holds a pattern."},
	{"PATTERN", "COPY TO", "Copies the playing pattern into a slot."},
	{"PATTERN", "TEMP TO", "Copies the playing pattern, Temp edits included, into a slot."},
	{"PATTERN", "CLEAR", "Resets a pattern slot to default."},
	{"PATTERN", "SWITCH", "When a queued pattern switch happens: at once, on the next beat, bar or pattern end."},

	{"SONG", "PATTERN", "The pattern the selected song entry plays."},
	{"SONG", "BARS", "How long the selected entry plays: - / + a bar, a preset, or PAT = the pattern's own length."},
	{"SONG", "EDIT",
	 "ADD: append the playing pattern. INSERT: a new entry before the selected one. DELETE: remove the "
	 "selected entry. CLEAR SONG: remove them all."},

	{"SETUP", "MIDI CH", "The selected track's MIDI channel (OFF = no MIDI). Device: context menu, MIDI."},
	{"SETUP", "CLOCK", "Sends MIDI clock (24 PPQN) with Start / Continue / Stop to the MIDI device. For the whole module."},
	{"SETUP", "TYPE", "NOTE: the track plays notes. CC: each pulse sends a value (Velocity) as MIDI CC and CV."},
	{"SETUP", "MODE",
	 "EUCLID: pulses, rotate and a note pool. SEQ: a step sequencer, each step its own notes, velocity and gate."},
	{"SETUP", "CYC MODE", "The edited cycle's own mode, EUCLID or SEQ (CYCLE page CLEAR > MODE drops it)."},
	{"SETUP", "CC x10", "The CC number's tens (0-119)."},
	{"SETUP", "CC x1", "The CC number's ones (0-119)."},

	{"RANDOM", "AMOUNT",
	 "Each parameter's random amount (set with RND ASSIGN). Click: C = new value each cycle loop, R = at the "
	 "Random rate. * = the edited cycle's own."},
	{"RANDOM", "SEQ",
	 "REGEN: new random values. PHASE: shift the sequence. LOCK: freeze the values. PROB: shift which notes "
	 "Probability skips. SLEW: glide between values (click steps it)."},
	{"RANDOM", "CLEAR", "Clears random amounts: ALL, or one parameter."},

	{"PHRASE", "PHRASE",
	 "The contour Range follows: CAD 1-4, waveforms, CV (IN expander Phrase CV) or USER (draw it on the right)."},
	{"PHRASE", "BARS", "Phrase length, in bars."},
	{"PHRASE", "LOOPS", "Phrase length, in loops of the track."},
	{"PHRASE", "SYNC", "BARS: the phrase runs on pattern time. LOOP: it restarts with the track's step loop."},

	{"SCALE", "SCALE",
	 "The scale the pool, Pitch and Range snap to. Editing NOTES turns it into the User scale. SEQ mode: "
	 "the keys, NOTE IN and the output snap to it, and changing it moves the sequence's notes onto it "
	 "(nearest note, ties up)."},
	{"SCALE", "ROOT", "The scale's root note. SEQ mode: the sequence's notes move onto the new scale."},

	{"STYLE", "STYLE", "The path through the Voicing positions over successive notes."},
	{"STYLE", "PLAY", "POLY: chords. MONO: arpeggiates the voiced pool, one note per event."},
	{"STYLE", "VOICE", "UP: Voicing takes the lowest note up an octave. DOWN: the highest down."},

	{"REPEAT", "MODE", "CHOKE: the next pulse cuts the repeats. TAIL: bursts overlap (up to 16)."},

	{"GROOVE", "GROOVE", "The Accent shape: percussion patterns (one bar) or waveforms (over the length)."},
	{"GROOVE", "BARS", "Waveform grooves' length, in bars. Percussion grooves are one bar."},
	{"GROOVE", "LOOPS", "Waveform grooves' length, in loops of the track. Percussion grooves are one bar."},
	{"GROOVE", "SWING", "Timing's subdivision: every 2nd unit of 1, 2 or 4 steps moves."},
	{"GROOVE", "SYNC", "BARS: grooves run on pattern time. LOOP: they restart with the track's step loop."},
};

/** The tip of row `label` on `page`, or nullptr. */
inline const char* rowTip(const char* page, const std::string& label) {
	for (const RowTip& r : kRows) {
		if (std::strcmp(r.page, page) == 0 && label == r.label)
			return r.text;
	}
	return nullptr;
}

// ---------------------------------------------------------------- context display: other areas
constexpr const char* kKeyPool = "Click: add / remove this note in the track's pool (in the scale).";
constexpr const char* kKeyStepPool = "Click: add / remove this note in the selected steps' own pool.";
constexpr const char* kKeySeqStep =
	"Click: add / remove this note in the selected step's chord. Keys off the scale are dimmed.";
constexpr const char* kKeySeqNone = "SEQ mode has no pool: click a step pad first, then add its notes here.";
constexpr const char* kKeyCc = "CC track: no notes.";
constexpr const char* kKeysOctave = "Shifts the keyboard an octave.";
constexpr const char* kKeysTrackPool = "Drops the selected steps' own pools: they play the track's pool again.";
constexpr const char* kKeysRest = "Makes the selected steps rests (removes their notes).";
constexpr const char* kScaleNotes = "Click: add / remove this note from the scale (it becomes the User scale).";
constexpr const char* kPhraseGrid = "USER phrase: click / drag draws the contour.";
constexpr const char* kLane = "Click / drag sets the lock, right-click removes it.";
constexpr const char* kLaneSeqVelocity = "The step's velocity. Click / drag sets it, right-click = the Velocity knob.";
constexpr const char* kLaneSeqGate =
	"The step's gate, in steps (more than 1 ties over the next ones). Click / drag sets it, right-click = 1.";
constexpr const char* kSongButton[] = {
	"Starts / stops song mode: the song plays its entries in order, each for its bars.",
	"Loops the song from the start when it ends.",
	"The grid follows the playing entry.",
};
constexpr const char* kSongArrow = "Previous / next page of song entries.";
constexpr const char* kSongCell = "Click: select this entry (PATTERN and BARS edit it). Mouse wheel: its bars.";
constexpr const char* kSongAdd = "Click: add the playing pattern as a new entry.";

} // namespace tips
