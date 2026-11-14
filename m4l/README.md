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
| `device/jezric_talea_label.js` | Draws a panel section's name turned on its side, in Live's theme colour. |
| `tests/sim.js` | The devices simulated with a fake Max, checked against the engine: `node m4l/tests/sim.js`. |
| `tests/test_pattern.js` | The pattern the simulation plays: off-grid timing, repeats, chords, a CC track. |

`m4l/build/` (not in git) gets the two `.amxd` files, a readable `.maxpat` of each, and
the `.js` files they load from their own folder.

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

## The panel (in progress)

- **On**: the whole sequencer. Off, it plays nothing (what's sounding ends on the next
  step). Switched on while Live plays, it starts at Live's next launch-quantization
  boundary (the Quantization menu in Live's control bar), as a clip launched then would;
  while stopped, at once. A stored parameter: saved, automatable, undoable. It isn't
  banged at load like the other controls (a bang toggles a `live.text`); the script reads
  its value at init.
- **Dials** in two rows of shaded sections, each named down its left side, as on the Rack module's panel: Shape and Repeats,
  then Groove and Tonal; every track parameter has one. Ranges, defaults and value names
  are read from the engine's tables (`src/core/Params.hpp`, `Values.hpp`) when the device
  is built. Rack's Harmony knob, and the Random knobs (for the display's RANDOM page),
  aren't here yet.
- **Track select** (1-16) along the top, like tabs over the sections: the track the dials edit.
- **Pads** under the sections, in the mode chosen in the **Pad mode** menu beside them.
  They are plain buttons, not Live parameters, and light as on the Rack module. Shift and
  Ctrl (Cmd on the Mac) are read with `[modifiers]`, polled by each press.
  - **Step**: a pad turns a step of the edit track off or on, taken on the next 16th like
    a dial. Orange is a hit, white the playing step, dark past Length. The **step page**
    tabs 1-4 in the left column show steps 1-16 ... 49-64 (a page past the track's Steps
    doesn't take); **Follow** moves the page with the playing step, and choosing a page
    turns it off, as on Rack.
  - **Mute**: a pad per track. A click stages a change (the pad blinks slowly);
    the **A** (Apply) button right of the menu, shown only while changes are staged, or
    choosing Mute again in the menu, commits them for the next mute boundary (blinking
    fast until then). The script shows and hides it through `[thispatcher]`. Ctrl+click mutes or
    unmutes at once. Leaving Mute mode drops what's staged. Orange plays (white on a note),
    blue-grey is muted. Stopped, Apply and Ctrl+click both mute at once.
  - **Pattern**: a pad per pattern slot. A click queues the pattern for the next pattern
    boundary (blinking), Shift+click adds it to the chain, Ctrl+click switches at once.
    Stopped, a click switches at once, the pattern starting with the song. Orange plays,
    dim red is in the chain, dim orange holds a pattern, dark is the default (silent)
    pattern. The dials and step pads follow the switch.
  - The mute timing and the pattern quantization are the engine's defaults for now
    (mutes Immediately, so on the next step; patterns at the end of the pattern). Their
    settings come with the display.
- **The display**, the right half: dark, in Live's LCD colours (the controls in their LCD
  appearance, `appearance 1`; the background, frame and labels take the theme's
  `live_lcd_bg`, `live_lcd_frame` and `live_lcd_title`). Page tabs along the top, as on
  the Rack module's display; each page's rows under them. The pages and rows are listed
  once, in `device/jezric_talea_display.js`: `build.py` builds the controls from it (all
  hidden), and the script shows the page's through `[thispatcher]`, sets them from the
  edit track and takes their edits on the next 16th, like a dial's. Built so far:
  - **TRACK**: the info line (steps, pulses, rotate, division, length; how many steps play
    when Length cuts the loop) and the track's steps, 16 to a row, lit as the pads are.
    Rack's clock and tempo aren't here.
  - **CYCLE**: all of the cycles' controls (Rack's Cycle pad mode and CYC EDIT button
    are here instead): COUNT (1-8), CYCLE (the selected one, one past the last adds
    cycles; "3*" has changes, "(3)" is muted), EDIT, MUTE (the selected cycle), CLEAR (ALL
    of its changes, HITS its step edits, or one override, listed by name), and a line
    saying which cycle plays. While playing, a bar under the CYCLE row marks the playing
    cycle (a panel per choice, shown by the script). **EDIT** on: the dials and the step pads edit the selected
    cycle (a dial overrides its value there; a step pad gives it step edits of its own),
    that cycle plays alone, TRACK shows it, and the CYCLE tab is framed red on every page.
    The other pages still edit the track: a cycle's own scale, pool, random amounts and
    locks aren't here yet, nor Rack's lanes of every cycle on TRACK.
  - **KEYS**: the note pool on a 4-octave keyboard (OCTAVE < > moves it, from C2 at first):
    a click adds a note or takes it out, keys outside the scale are greyed (a pool note
    stays clickable), the root is marked, a note lights as it starts.
  - **SCALE**: scale (a menu) and root, and the scale's notes on one octave: a click adds
    or removes one, making the scale the User scale (as on Rack).
  - **PHRASE**: shape, bars or loops, sync. Rack's CV input shape isn't here.
  - **VOICING** (Rack's STYLE page, named for the dial it goes with): style, poly or mono,
    voicing direction. (No REPEAT page: Choke or Tail is on SETUP.)
  - **GROOVE**: shape, bars or loops (dimmed for the one-bar percussion grooves), swing
    subdivision, sync.
  - **RANDOM**: the edit track's randomisation, set on the page (no Rnd assign mode: the
    dials always show values). Each of the 15 parameters that can be randomised (not
    Steps, Division, Length) has its amount (-100..100 %) and R/C: a new value per Random
    Rate tick or per cycle loop. Then Variance, Rate, Slew, Lock (locks the sequences where
    they are now), and REGEN, PHASE -/+, PROB -/+ and CLEAR ALL.
  - **PATTERN**: Play (queues, as a pattern pad), Copy to, Clear, Switch timing. A slot
    holding a pattern reads "3*" in all three rows: the script renames the choices
    (`_parameter_range`), and ignores what a row sends within 100 ms of a rename, in case
    Live echoes it as a click.
  - **SETUP**: Type (note or CC), CC number (dimmed for a note track), Repeats (Choke or
    Tail: a new pulse cuts the repeats before it, or they overlap), and the device's
    own Instance, Look-ahead and status line. Rack's MIDI channel and clock aren't here:
    each receiver picks its track.
  - **Mute mode** shows its settings over any page, as on Rack: Apply timing (Now, Beat,
    Bar, Pattern) and On mute (Finish or Cut). A page tab leaves Mute mode.
  - **Diagrams**, in a band under the rows, and the TRACK and KEYS boards (`jezric_talea_diagram.js`, a `[v8ui]` in the
    theme's LCD colours, from `[live.colors]`), as on Rack's display: GROOVE's accent
    shape over its span (dim the full shape, bright scaled by Accent) with a playhead;
    PHRASE's notes over the phrase, or for the User shape its grid, which a click or drag
    edits; VOICING's next 16 notes. The engine works out the points (`talea_diagram`); the
    script sends them when they change and the playhead on each 16th.
  - The other pages say they aren't built yet.
  - Turning a dial shows its page, as Rack's "display follows knobs": the shape dials
    TRACK, the repeat dials SETUP (Choke / Tail), the groove dials GROOVE, Pitch SCALE, Voicing VOICING,
    Range PHRASE (`dialPages` in the spec). In Mute mode the Mute view stays.
  The mute and switch timings are saved in the bank, as Rack saves them.

## Edits

A turn is taken on the next 16th: the sequencer puts the engine back as it was after the step playing
now (it keeps the engine's state after each of the last steps), works out the steps it
had already sent again with the new value, and sends them over the old ones. So an edit
is heard from the next step, not look-ahead steps later.

The engine's saved state after each step holds everything playing changes: the sounding
notes and repeats, but also the playing pattern with its mutes, the mutes and pattern
switch waiting for their boundaries, the chain's position and the song's. So an edit made
while a mute or switch the sequencer has already worked out ahead is still to be heard
doesn't undo it: the engine goes back to the state at the step playing, then takes the
edit. Mutes, Apply and pattern pads are taken on the next 16th the same way. The pads show
the mutes and patterns of the step playing, not of the steps worked out ahead.

Engine time is Live's song time. A switch starts the new pattern at its song time; when
Live jumps back before it (a loop wrapping, Play from earlier), the engine runs the
pattern from the song's start again, and a queued switch or mutes wait for their
boundaries counted from the new position (`Engine::jump`).

The engine starts with its default pattern, which is silent (no pulses), as a new Talea
in Rack.

## The pattern, saved with the set

The sequencer keeps the whole bank (the 16 pattern slots that hold something, each the
Rack module's pattern JSON of about 13 KB, and which one plays, as Rack's patch saves
them) in a `[pattr]` that is a Live parameter of type Blob named Pattern, visibility
**Stored Only**: Live saves it with the set and gives it back when the set loads, before
the device starts. Mutes are in it (a pattern's tracks carry their mute), and it's stored
after a mute change and after a pattern switch, as after an edit. Things that matter:

- **Stored Only**, not Hidden: Hidden parameters aren't saved.
- For a plain object like `[pattr]`, Parameter Mode Enable goes in the box's
  `saved_object_attributes`, as in Ableton's own devices. On the box itself (where UI
  objects have it) it's silently ignored and nothing is saved.
- The value is a list: the format number (2: the bank; 1, from sets saved before it: one
  pattern, which loads as pattern 1), then base64 pieces each after a "p". JSON's
  quotes, commas and braces are Max syntax; a list must start with a number, or Max
  takes its first word as a message name; the "p" keeps a piece from being read as a
  number.
- The script stores the pattern once edits pause for half a second (each edit restarts a
  `[delay]`), at low priority. So a dial turned through many values is stored once. An
  edit in the half second before saving the set isn't in the saved file.
- `[pattr]` sends each value straight back; the script ignores a value equal to the one
  it holds.

**Undo and redo:** each stored pattern is one step in Live's undo history ("Change in
Talea Sequencer"). Live gives `[pattr]` the earlier value back; the whole pattern is taken
on the next 16th, like an edit (step toggles and all), and nothing is stored back, so redo
still works. Edits not yet taken give way to it.

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
