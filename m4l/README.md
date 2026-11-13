# Talea for Max for Live

Talea in Ableton Live, as two Max MIDI Effects:

- **Talea Sequencer**: one per set, on any MIDI track. Runs Talea's engine (the same
  C++ as the Rack module, compiled to WebAssembly) and holds the pattern.
- **Talea Track Receiver**: one per instrument track, before the instrument. Plays one
  Talea track (**Track** 1-16) as MIDI.

Sequencer and receivers find each other by **Instance** (A-D), so a set can have up to four
independent sequencers.

The design was proved in a separate proof of concept (recorded tests in Live: note
timing within 0.2 ms, chords, CC glides, edits during play, the pattern saved with the
set, undo, CPU load). This folder is that design, without the test scaffolding.

## Files

| Path | What it is |
|---|---|
| `engine/` | Builds the engine as WebAssembly for Max's v8: `python m4l/engine/build.py` (needs Emscripten). |
| `device/build.py` | Builds the devices into `m4l/build/`: `python m4l/device/build.py`. |
| `device/jezric_talea.js` | The script both devices run (`[v8 jezric_talea.js brain]` in the sequencer, `receiver` in the receiver). |
| `device/jezric_talea_core.js` | Runs the engine a step at a time and turns its events into MIDI entries. |
| `tests/sim.js` | The devices simulated with a fake Max, checked against the engine: `node m4l/tests/sim.js`. |
| `tests/test_pattern.js` | The pattern the simulation plays: off-grid timing, repeats, chords, a CC track. |

`m4l/build/` (not in git) gets the two `.amxd` files, a readable `.maxpat` of each, and
the three `.js` files they load from their own folder.

To build and test:

    python m4l/engine/build.py
    node m4l/tests/sim.js
    python m4l/device/build.py

Any recent Node runs the simulation; Emscripten has one (`emsdk/node/<version>/node.exe`).

## How it plays on time

A receiver's script never plays a note itself. For each 16th step the sequencer sends every
track's MIDI entries (offset within the step, a note to end, a note to start and its
velocity; or a CC value with its glide), look-ahead steps early (**Look-ahead**, default
4, minimum 4). The receiver writes its track's entries into a native `[coll]` keyed by
step. On each 16th the patch looks the step up and hands each entry to `[pipe]`, delayed
by its offset at Live's current tempo, all on Max's high-priority scheduler. So the
script, which Live's interface can hold up, may fall behind by up to look-ahead steps
without a note being late. Each step is deleted from the `[coll]` as it plays, so a note
can't play twice.

While stopped, the sequencer sends the steps from Live's start marker, so the first note
after Play is on time too. Stop, Play from elsewhere, a timeline click or a loop wrap
restarts the engine's run from the new step; the sequencer counts these as epochs, so
receivers drop entries for the old position. After a click while playing, the step
clicked to can be lost: the sequencer only hears of the jump on that step. If the loop is
switched on or off or moved within look-ahead of its end, the sequencer sends the steps for
the right side of it again.

A CC track's value travels as `offset -1 ccNumber value glide`; after `[pipe]`,
`[route -1]` sends it to `[line]`, which moves the controller to the value over the glide
and sends each new whole number as CC on channel 1. A chord is one entry per note.

## Edits

For now the sequencer has an **Edit track** menu and dials for Pulses, Rotate, Repeats,
Pace, Probability, Sustain, Timing and Delay (the real controls come next). A turn is
taken on the next 16th: the sequencer puts the engine back as it was after the step playing
now (it keeps the engine's state after each of the last steps), works out the steps it
had already sent again with the new value, and sends them over the old ones. So an edit
is heard from the next step, not look-ahead steps later.

The engine starts with its default pattern, which is silent (no pulses), as a new Talea
in Rack.

## The pattern, saved with the set

The sequencer keeps the whole pattern (the Rack module's pattern JSON, about 13 KB) in a
`[pattr]` that is a Live parameter of type Blob named Pattern, visibility **Stored
Only**: Live saves it with the set and gives it back when the set loads, before the
device starts. Things that matter:

- **Stored Only**, not Hidden: Hidden parameters aren't saved.
- For a plain object like `[pattr]`, Parameter Mode Enable goes in the box's
  `saved_object_attributes`, as in Ableton's own devices. On the box itself (where UI
  objects have it) it's silently ignored and nothing is saved.
- The value is a list: the format number 1, then base64 pieces each after a "p". JSON's
  quotes, commas and braces are Max syntax; a list must start with a number, or Max
  takes its first word as a message name; the "p" keeps a piece from being read as a
  number.
- The script stores the pattern once edits pause for half a second (each edit restarts a
  `[delay]`), at low priority. So a dial turned through many values is stored once. An
  edit in the half second before saving the set isn't in the saved file.
- `[pattr]` sends each value straight back; the script ignores a value equal to the one
  it holds.

**Undo and redo:** each stored pattern is one step in Live's undo history ("Change in
Talea Sequencer"). Live gives `[pattr]` the earlier value back; the values that differ
become edits, taken like a dial's, and nothing is stored back, so redo still works.

**The dials are Hidden parameters:** not saved, automated or undone themselves. Their
values are the pattern's and the script sets them from it. As stored parameters, setting
them at load marked the set as changed, and each turn was an undo step of its own. The
Edit track menu, Instance and Look-ahead are stored as usual.

## Messages and debugging

Warnings and errors go to the Max Console: a step that wasn't ready in time ("MISS"), one
that came after it should have played ("LATE"), an unreadable saved pattern, and any
error in the script, with where it happened. The receiver's status line counts notes,
misses and late steps.

Each device's patch has a **debug log** toggle (in the patch, not on the panel). With it
on, the Console also gets a line for each edit, each Play (with the values changed so
far), undo and redo, each stored pattern, the script being held up ("script ran ... ms
late"), and at each stop the script's time per step and, for receivers, how few steps
ahead a step arrived.

## Timing and CPU (measured in the proof of concept)

Six tracks, look-ahead 4, 120 BPM, playing normally, adding VSTs and opening their
windows during play: the sequencer about 0.15-0.2 ms per step including sending to the
receivers (max 4 ms), receivers about 0.005 ms (max 1 ms). Every step arrived 4 steps
ahead, with no misses or late steps. Adding a VST stops Max's scheduler with the rest of
Live (up to 18 s); afterwards playback carries on in place. Notes in the recordings were
within 0.2 ms of the engine's times, chords within 0.02 ms.

Each engine takes about 7 MB of the module's fixed 64 MB.

## Known limits

- 4/4 only.
- Switching the loop on or off, or moving it, within a step or two of its end can lose
  those steps: Live tells scripts of loop changes late.
- After a click on the timeline while playing, the step clicked to can be lost.
- The devices load the `.js` files from their own folder; they aren't frozen yet.
