# Talea — Panel & Interface Spec (v0.3 draft)

A 16-track Euclidean/algorithmic sequencer for VCV Rack 2, designed for a
screen-and-mouse environment.

Inspired in part by hardware algorithmic sequencers, including the Torso T-1. Not
affiliated with or endorsed by Torso Electronics.

Parameters are grouped into four families — Shape, Groove, Tonal and Setup. Items marked
**[Rack]** are features specific to this VCV Rack module implementation.

---

## 1. Design principles

1. **No shift layers.** Every secondary function gets its own control or a display
   field. Nothing hides behind a modifier key: hardware-style key-combos become
   explicit buttons, modes or display pages.
2. **Amounts on knobs, selections on the display.** Continuous amounts (Pulses, Range,
   Accent…) are panel knobs. List selections (Style, Phrase, Groove, Scale, Quantize,
   repeat mode…) live on contextual display pages where every option is visible by name.
3. **Display follows focus.** Touching a knob switches the display to that knob's page
   (e.g. turning Range shows the Phrase page). Can be disabled in the context menu
   (*Display follows knobs*). Shape knobs → TRACK, Repeats group → REPEAT, Groove group →
   GROOVE, Pitch / Harmony → SCALE, Voicing → STYLE, Range → PHRASE, Variance / Rate →
   RANDOM; under Rnd assign randomisable knobs → RANDOM; with steps selected lockable knobs
   → STEP (lock lanes).
4. **Pads do one job at a time, and the job is always visible.** Mode buttons with lights.
5. **Knobs follow the selected track** (and selected cycle / selected step when those edit
   modes are active). On selection change the module writes the stored values back into
   the knobs, so knobs always show the truth. CV and random never move a knob: an orange
   **modulation ring** (arc from the knob's value to the live value, with a dot) shows
   where the selected track's parameter plays right now, and the page's info line shows
   `stored>live` (e.g. `VEL 64>87`).
6. **Mouse modifiers replace button holds.** Ctrl/Shift-click on pads for multi-select,
   right-click for per-element menus (clear, copy, reset to default).
7. **Everything is undoable.** Every edit made on the panel (pads, buttons, display rows,
   menus, copy / paste / clear) is one step in Rack's undo history, like knob turns.
   **[Rack]** UI-only state (selected track / cycle, pad mode, display page) isn't recorded.

---

## 2. Panel layout (proposed, ~40 HP — may need 44 HP, decide at SVG draft)

The sketch below is the original proposal; the generated panels in `docs/panel/` (Talea,
Talea IN, Talea OUT; `tools/gen_panel.py`) are the current layout.

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ Talea                                                                       │
│ ┌[TRANSPORT]────┐ ┌───────────── CONTEXT DISPLAY (D01) ──────────────────┐ │
│ │ PLAY RST TAP  │ │ [tabs: TRACK STEP CYCLE KEYS SCALE PHRASE STYLE      │ │
│ │ TEMPO         │ │        REPEAT GROOVE RANDOM PATTERN SONG SETUP]      │ │
│ │ CLK RST RUN in│ │                page content                          │ │
│ │ TEMP  ALL  RND│ │                                                      │ │
│ └───────────────┘ └──────────────────────────────────────────────────────┘ │
│                                                      DATA (K40)  ENTER      │
│ ┌[SHAPE]─────────────────────────┐ ┌[REPEATS]──────────────┐               │
│ │ STEPS PULSES ROTATE DIV LENGTH │ │ REPEATS RAMP TIME PACE│               │
│ └────────────────────────────────┘ └───────────────────────┘               │
│ ┌[GROOVE]───────────────────────────────────┐ ┌[TONAL]─────────────────┐  │
│ │ VELOCITY PROB SUSTAIN ACCENT TIMING DELAY │ │ PITCH HARMONY VOICING RANGE│
│ └───────────────────────────────────────────┘ └────────────────────────┘  │
│ ┌[RANDOM]──────────┐                                                       │
│ │ VARIANCE RATE    │                                                       │
│ └──────────────────┘                                                       │
│ [TRACK][STEP][CYCLE][MUTE][PATTERN]   [CYC EDIT] [COPY][PASTE][CLEAR]      │
│ ┌──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┐                          │
│ │ 1│ 2│ 3│ 4│ 5│ 6│ 7│ 8│ 9│10│11│12│13│14│15│16│                          │
│ └──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┘                          │
│ MOD 1–4 + atten   TRANSPOSE  PHRASE CV  PATTERN  TEMP   PITCH GATE VEL CLK RST EOP│
└──────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Panel elements

ID prefixes: **K** knob · **B** button · **J** jack · **L** light · **D** display.
Hover tooltips (every element's short name and description, and the context display's tips):
[`src/rack/Tooltips.hpp`](../src/rack/Tooltips.hpp).
`Enum` is the C++ identifier.

### 3.1 Transport, clock & performance

| ID  | Name       | Type   | Enum                 | Range / values                  | Default | Behaviour                                                                                                                                |
| --- | ---------- | ------ | -------------------- | ------------------------------- | ------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| B01 | Play       | button | `PLAY_PARAM`       | toggle                          | off     | Play / pause: resumes from the current position; Reset (B02) returns to step 1. Pausing releases all gates.                          |
| L01 | Play       | light  | `PLAY_LIGHT`       | —                              | —      |                                                                                                                                          |
| B02 | Reset      | button | `RESET_PARAM`      | momentary                       | —      | All tracks to step 1. *Kill stuck notes* (all gates low) is in the module's context menu.                                                                |
| B03 | Tap        | button | `TAP_PARAM`        | momentary                       | —      | Tap tempo (internal clock only).                                                                                                         |
| K01 | Tempo      | knob   | `TEMPO_PARAM`      | 24–280 BPM                     | 120     | Internal clock; ignored when J01 patched.                                                                                                |
| L02 | Clock      | light  | `CLOCK_LIGHT`      | —                              | —      | Quarter-note blink.                                                                                                                      |
| J01 | Clock in   | input  | `CLOCK_INPUT`      | trigger                         | —      | PPQN in context menu (1, 2, 4, 8, 12, 24, 48, 96), default 4. Positions between pulses are interpolated from the measured period.      |
| J02 | Reset in   | input  | `RESET_INPUT`      | trigger                         | —      |                                                                                                                                          |
| J03 | Run in     | input  | `RUN_INPUT`        | gate / trigger                  | —      | Gate = run while high; trigger = toggle (menu).                                                                                          |
| B58 | Rec        | button | `REC_PARAM`        | toggle                          | off     | **[Rack]** SEQ mode recording (§3.15): only while the selected track (the edited cycle in CYC EDIT) is in SEQ mode. Light solid = live recording, blinking = step recording (stopped). |
| B59 | Overdub    | button | `OVERDUB_PARAM`    | toggle (saved)                  | off     | **[Rack]** Recording only (§3.15): on = notes played on a step that already has notes are added to its chord; off = they replace them. Light on while enabled. |
| J26 | Note in V/Oct | input | `REC_PITCH_INPUT` | V/Oct, poly                    | —      | **[Rack]** Keyboard pitch for recording (e.g. from VCV MIDI-CV); channel n goes with gate channel n. |
| J27 | Note in Gate  | input | `REC_GATE_INPUT`  | gate, poly                     | —      | **[Rack]** One channel per held key.                                                                                                     |
| J28 | Note in Vel   | input | `REC_VELOCITY_INPUT` | 0–10 V, poly                | —      | **[Rack]** Unpatched: the Velocity knob.                                                                                                 |
| B04 | Temp       | button | `TEMP_PARAM`       | click toggles (one mouse can't hold it and turn a knob) | off     | While active, every pattern edit (knobs, steps, cycles, random, page options) is a temporary overlay. Click again = revert; **Ctrl+click = keep** the changes; right-click menu: Keep / Discard. Mutes are never reverted; mutes staged during Temp apply when it ends. Saving while Temp is on saves the pattern without the overlay. Display shows a yellow TEMP tag. |
| L04 | Temp       | light  | `TEMP_LIGHT`       | —                              | —      | Solid = held by J04, blinking = latched by B04.                                                                                                      |
| J04 | Temp in    | —      | moved to the IN expander (Y-J19, §3.12b), v0.2 | | |
| B05 | All        | button | `ALL_PARAM`        | toggle                          | off     | While on, the group (see B42–B57) is every track: plain knob turns apply **relatively to all tracks** in the pattern (header: ALL). Combined with Temp: Temp reverts the whole pattern, so relative all-track edits made during it are reverted too.                            |
| L05 | All        | light  | `ALL_LIGHT`        | —                              | —      |                                                                                                                                          |
| B06 | Rnd assign | button | `RND_ASSIGN_PARAM` | toggle                          | off     | While lit, turning any knob sets that parameter's**random amount** (bipolar: CW / CCW behaviour, §4 RANDOM) instead of its value. |
| L06 | Rnd assign | light  | `RND_ASSIGN_LIGHT` | —                              | —      |                                                                                                                                          |

### 3.2 Context display & navigation

| ID  | Name            | Type           | Enum                        | Range / values | Default | Behaviour                                                        |
| --- | --------------- | -------------- | --------------------------- | -------------- | ------- | ---------------------------------------------------------------- |
| D01 | Context display | display        | `ContextDisplay` (widget) | see §4        | TRACK   | Mouse-interactive. Tab strip along top doubles as page selector. |
| D01 size | | | | | | v0.2: the display spans the full height down to the knob rows (8–55 mm). |

Page selection is by clicking display tabs (or display-follows-focus); no separate page
buttons — saves ~11 buttons of panel space. Data (K40) / Enter (B40) were removed in v0.2:
the display is edited by mouse.

### 3.3 Shape (selected track · selected cycle when CYC EDIT on)

| ID  | Name     | Type | Enum               | Range / values                                                     | Default | Behaviour                                                                                         |
| --- | -------- | ---- | ------------------ | ------------------------------------------------------------------ | ------- | ------------------------------------------------------------------------------------------------- |
| K02 | Steps    | knob | `STEPS_PARAM`    | 1–`kMaxSteps` (64)                                              | 16      | Grid length.                                                                                      |
| K03 | Pulses   | knob | `PULSES_PARAM`   | 0–Steps                                                           | 0       | Euclidean hit count. Default 0 = new track is silent until pulses are set. 0 also used by FX Hold drone (v0.3). |
| K04 | Rotate   | knob | `ROTATE_PARAM`   | 0–(Steps−1)                                                      | 0       | Shifts whole pattern incl. manual hits.                                                           |
| K05 | Division | knob | `DIVISION_PARAM` | 1/1, 1/2, 1/4, 1/8, 1/16, 1/32, 1/64; triplet variants; free (continuous). Mode on REPEAT page | 1/16    | Rate of each step. Free = continuous between note values.                                         |
| K06 | Length   | knob | `LENGTH_PARAM`   | 1/64, 1/32, 1/16, 1/8, 1/4, 1/2, 1–8 bars, 16 bars, ∞            | ∞       | Loop length of the track**independent of Steps** — uneven combos drift against each other. Default ∞ so Steps alone sets the loop; steps Length cuts off are drawn faint (display and pads) and the info line shows how many play.**[Rack]** |

### 3.4 Repeats (Note Repeater)

| ID  | Name    | Type | Enum                  | Range / values                                     | Default | Behaviour                                                                |
| --- | ------- | ---- | --------------------- | -------------------------------------------------- | ------- | ------------------------------------------------------------------------ |
| K07 | Repeats | knob | `REPEATS_PARAM`     | 0–48, ∞ at max                                   | 0       | Extra triggers after each pulse. Mode Choke (next pulse cuts the repeats) / Tail (bursts overlap, up to 16 per track; a repeat landing on a note already starting at that instant is dropped) on REPEAT page. ∞ = repeats until the pulse comes round again (one pass of Steps, or Length if shorter). Repeat gate = Sustain applied to the spacing to the next repeat; the pulse's own gate also follows the repeat spacing while Repeats > 0. Reset / seek drops repeats in flight. |
| K08 | Ramp    | knob | `RAMP_PARAM`        | bipolar ±100 %                                    | 0       | Velocity across repeats, relative to Velocity, linear. CW: first repeat at (1 − Ramp) × Velocity, building back to Velocity on the last. CCW: falls from Velocity to (1 − \|Ramp\|) × Velocity on the last. With ∞ the ramp spans 16 repeats then holds. |
| K09 | Time    | knob | `REPEAT_TIME_PARAM` | 15 quantized values: 1 · 1/2, 1/2d, 1/2t · 1/4, 1/4d, 1/4t · 1/8, 1/8d, 1/8t · 1/16, 1/16d, 1/16t · 1/32 · 1/64 (d = dotted, t = triplet) | 1/16 | Spacing between repeats. Always snaps to a note value (no free mode).                                                 |
| K10 | Pace    | knob | `PACE_PARAM`        | bipolar, centre = even                             | centre  | CW = repeats progressively**slow down**, CCW = **speed up**. Gap before repeat i+1 = Time × 2^(Pace/100 × i/4): full CW doubles the gap every 4 repeats, full CCW halves it; limited to 8× either way. |

### 3.5 Groove

| ID  | Name        | Type | Enum                  | Range / values                      | Default           | Behaviour                                                                                                                                                                |
| --- | ----------- | ---- | --------------------- | ----------------------------------- | ----------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| K11 | Velocity    | knob | `VELOCITY_PARAM`    | 0–127                              | 100               | Base velocity.                                                                                                                                                           |
| K12 | Probability | knob | `PROBABILITY_PARAM` | bipolar, centre = 100 % play        | centre            | CW = skip applies to all notes incl. repeats; CCW = pulses only (skipped pulse skips its repeats). Skip pattern follows a fixed random sequence keyed on each note's grid position, so the same notes skip on every replay and the knob only sets how many (seed/phase shift on RANDOM page, later). A skipped pulse plays no repeats and doesn't choke earlier ones. |
| K13 | Sustain     | knob | `SUSTAIN_PARAM`     | shortest → very long (overlapping) | 50 % = 1 division | Note length relative to division: 0–100 % = 0–2 divisions (above 50 % notes overlap; the gate retriggers).                                                                                                                                        |
| K14 | Accent      | knob | `ACCENT_PARAM`      | 0–100 %                            | 0                 | Bipolar velocity variation around Velocity, shaped by**Groove** (selected on GROOVE page). 100 % = ±63 velocity at the shape's peaks, evaluated at each note's time (repeats included). Percussion grooves (Agogo, Timbales, Congas, Bongo) are original one-bar 1/16 accent patterns; waveforms (Saw, Triangle, Sine, Pulse) span the Groove length. **[Rack]** GROOVE page SYNC: BARS (default) runs the shapes on pattern time; LOOP counts Groove length in loops of the track and restarts the accents with step 1 (percussion bars count from each loop's start), so a loop that isn't a whole number of bars (e.g. 14 × 1/16) doesn't drift against them. |
| K15 | Timing      | knob | `TIMING_PARAM`      | bipolar, centre = straight          | centre            | Shifts every 2nd step later (CW) / earlier (CCW) = swing. Subdivision ×1/×2/×4 on GROOVE page: every 2nd unit of 1/2/4 steps moves; full Timing = half a unit. Counted from the start of each Length loop. Repeats follow their (shifted) pulse. |
| K16 | Delay       | knob | `DELAY_PARAM`       | bipolar, centre = 0                 | centre            | Shifts the whole track earlier/later vs the grid, ±100 % = ±1 step. Notes pulled before the start of playback are skipped on the first pass. |

### 3.6 Tonal

| ID  | Name    | Type | Enum              | Range / values          | Default | Behaviour                                                                                                             |
| --- | ------- | ---- | ----------------- | ----------------------- | ------- | --------------------------------------------------------------------------------------------------------------------- |
| K17 | Pitch   | knob | `PITCH_PARAM`   | ±4 octaves (±48 semitones, piano span), snapped to scale | 0 | Transposes the track's**note pool** in semitones; the result is snapped to the scale (nearest scale note, ties down), so with a scale some knob positions give the same notes. Pool itself is entered in the KEYS page (default pool: C4 alone). |
| K18 | Harmony | knob | `HARMONY_PARAM` | relative (endless feel) | —      | Shifts**one chord tone at a time**: each CW detent moves the lowest pool note up one scale step, each CCW detent the highest down one, skipping notes already in the pool. Hysteretic (turning back moves a different note; alternating directions contracts the pool towards a cluster). Edits the pool itself. Right-click →*Restore pool*: back to the pool the current run of detents started from, for the whole group. A run ends when anything else edits the pool (KEYS, paste, clear, random) or the edit target changes (track vs. a cycle's own pool), so the next detent starts a fresh one. **[Rack]** Session state, not saved with the patch; one step in Rack's undo history like any other panel edit. |
| K19 | Voicing | knob | `VOICING_PARAM` | 0–16 motion steps      | 0       | Amount of motion applied by the selected Style: position p = p moves, each taking the lowest note up an octave (or the highest down, VOICE = DOWN). Style sets the path through positions 0…Voicing over successive notes: Fixed = always Voicing; Ramp 0,1…V,0…; Climb 0,1,2,1,2,3…; Up/Down 0…V…1; Climb Up/Down = Climb folded back at V. Motion counter: Choke = every note played (pulses and repeats) advances it, restarting each Length loop; Tail = pulses play position 0 and each burst's repeats advance it. **Mono** arpeggiates the voiced pool one note per event (the voicing moves on after each full pass); **Poly** chords play their lowest note on the main outputs (full chords on the expander, later). Style chosen on STYLE page. |
| K20 | Range   | knob | `RANGE_PARAM`   | 0–48 semitones (12-tone steps up to 4 octaves); display shows e.g. "1 oct + 5" | 0 | Adds up to Range semitones above the pool, following the selected**Phrase** (0 = pool, 1 = +Range) over the Phrase length; the sum is snapped to the scale. Cadence 1–4 are original 8-segment contours; Saw/Triangle/Sine/Pulse are waveforms; CV input maps −5…+5 V to 0…1; User is a drawn 16-column contour (PHRASE page). **[Rack]** PHRASE page SYNC: BARS (default) runs the phrase on pattern time; LOOP counts Phrase length in loops of the track (each cycle's own loop with cycles), so the phrase restarts with step 1 however long the loop is. Every shape is a contour, not pitches: Range alone decides how far it reaches, and the result is snapped to the scale, so two levels can land on the same note. |

Scale and Root are selected on the SCALE page (list + user-scale editor), not knobs.

### 3.7 Random

| ID  | Name     | Type | Enum                  | Range / values | Default | Behaviour                                                                         |
| --- | -------- | ---- | --------------------- | -------------- | ------- | --------------------------------------------------------------------------------- |
| K21 | Variance | knob | `VARIANCE_PARAM`    | 0–100 %       | 0       | Per track. Chance that each value of a 16-value random sequence is re-rolled each time the sequence comes round (0 = the same 16 values forever, 100 = fresh every pass). |
| K22 | Rate     | knob | `RANDOM_RATE_PARAM` | 1/64 … 4 bars (note divisions, triplets) | 1/16 | Per track. How fast "Random Rate" targets advance (per-cycle targets follow cycles instead). |

Per-parameter amounts are set with **B06 Rnd assign** (knob centre = 0, full CW = +100 %,
full CCW = −100 %) and cleared on the RANDOM page. **Steps, Division and Length can't be
randomised** (their knobs are locked and dimmed under Rnd assign): they set the loop length, and a random loop length would leave the track and
pattern without a fixed length. A positive amount pushes the value up by up to amount × the
parameter's range, a negative one down; Pulses moves by up to amount × Steps, Rotate wraps
around the steps, random Repeats stays finite, and Probability's amount adds skip chance.

### 3.8 Pads, pad modes & edit actions

| ID       | Name             | Type      | Enum                   | Behaviour                                                                                                                                                                                                                                                         |
| -------- | ---------------- | --------- | ---------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| B11      | Mode: Step       | button    | `MODE_STEP_PARAM`    | Pads = 16 steps of current step page (B36–B39). Click = toggle pulse (adds and removals are sticky per step; removing a generated hit leaves the other hits in place, clicking again restores it). Ctrl+click = select step(s) for **locking** (blue; Ctrl+click again deselects): the knobs of lockable parameters (Repeats, Ramp, Time, Pace, Velocity, Probability, Sustain, Accent, Timing, Delay, Pitch, Voicing, Range) show the first selected step's value and write it as a lock on every selected step; Steps / Pulses / Rotate / Division / Length knobs lock and dim. Outside CYC EDIT locks are the track's (every cycle plays them); in CYC EDIT they go to the edited cycle only, over the track's (* in the info line; CYCLE page CLEAR › LOCKS). Clicking STEP again, leaving STEP mode or changing track ends the selection. Display: LOCK tag; the info line lists the selected steps and the first one's locks. Steps with locks are light blue (pads) / have a light-blue bar (step grid and lanes). Copy / paste / clear steps with B17–B19 (pool locks included). **Step pool**: on the KEYS page a selected step can get its own note pool (a chord or single note), replacing the track's (or cycle's) pool on its pulse and repeats; Pitch (and its lock), Voicing, Style, Poly / Mono, Range and Phrase still apply. Harmony moves the track's pool only. Retrigger later. With several cycles, outside CYC EDIT the pads show the track's steps and mark how the **playing cycle** differs: dim red = the cycle adds the step, dark orange = the cycle drops it. |
| B12      | Mode: Cycle      | button    | `MODE_CYCLE_PARAM`   | Pads 1–8 = cycle slots 1–8 (pads 9–16 unused). Click = select cycle; clicking past the last adds cycles. Ctrl+click = mute cycle. Lit: orange = selected (red while CYC EDIT), blue-grey = muted, dim red = has overrides; the playing cycle flashes white. Count / mute / clear on the CYCLE page.**[Rack]** |
| B13      | Mode: Keys       | —         | `MODE_KEYS_PARAM`    | **Retired (v0.2)**: the display's KEYS page (§4) replaced the KEYS pad mode; the button is gone from the panel (MUTE and PATTERN moved left), its id stays reserved. Saved patches in KEYS mode open in STEP mode. |
| B14      | Mode: Mute       | button    | `MODE_MUTE_PARAM`    | Click enters MUTE mode. Pads: orange = playing, blue-grey = muted. Click a pad to stage a change (it blinks to its new state); click MUTE again to apply all staged changes; leaving the mode drops them. Ctrl+click a pad mutes/unmutes at once. Right-click MUTE: apply / clear pending, apply timing, on-mute behaviour. MUTE is a view like the others: the display shows the mute view, and clicking a page tab leaves MUTE mode (pads back to the mode before it, staged changes dropped); unmuting needs MUTE mode again. **[Rack]** |
| B15      | Mode: Pattern    | button    | `MODE_PATTERN_PARAM` | Selects PATTERN pad mode: pads = the 16 pattern slots (orange = playing, blinking = queued, dim red = in the chain, dim = holds a pattern). Click a pad = queue it (switches at the Switch-timing boundary). Shift+click pads = build a chain (first entry queued, then each pattern end moves to the next, wrapping round); a plain click ends the chain. Ctrl+click = switch at once. Stopped, every selection switches at once. A switch restarts all tracks from step 1, cuts sounding notes and repeats, ends Temp (reverting it) and drops pending mutes; edits stay in the pattern's slot. Light blinks while a switch is queued. Right-click: switch timing, clear chain.                                                                                                                                                                              |
| L10–L15 | Mode lights      | light     | `MODE_LIGHTS + n`    |                                                                                                                                                                                                                                                                   |
| B16      | Cycle edit       | button    | `CYCLE_EDIT_PARAM`   | On: knob edits override the parameter in the **selected cycle**, and only that cycle plays. Off: knob edits move the track value and every cycle override by the same amount. Display shows a red "CYC n EDIT" tag. |
| L16      | Cycle edit       | light     | `CYCLE_EDIT_LIGHT`   | Red while on.                                                                                                                                                                                                                               |
| B17      | Copy             | button    | `COPY_PARAM`         | Click arms (click again disarms); the next click copies its target and disarms: **track button** (whole track: values, steps, pool, scale, cycles, random, mute), **CYCLE pad** (the cycle's overrides, own steps, random overrides, mute), **PATTERN pad** (the slot as it plays, Temp included) **STEP pad** (on/off state and locks) or **knob** (one parameter's value; the selected step's lock while steps are selected, the edited cycle's in CYC EDIT). The clipboard is outside the patterns, so it pastes across them. A knob clicked while armed doesn't turn. **[Rack]**                                                                                                                                                             |
| B18      | Paste            | button    | `PASTE_PARAM`        | Click arms (only with a clipboard); every matching target clicked receives it until PASTE is clicked again: track → track buttons; cycle → CYCLE pads (pasting past the last adds cycles); pattern → PATTERN pads; step → STEP pads (on/off and locks); parameter → track buttons (set like a knob turn, cycle overrides move with it) or CYCLE pads (override in that cycle). Other clicks act as usual. **[Rack]**                                                                                                                                                                               |
| B19      | Clear            | button    | `CLEAR_PARAM`        | Click arms; the next click clears its target and disarms (Ctrl+click CLEAR, also when already armed: stays armed until plainly clicked again): track button → track to default; CYCLE pad → cycle cleared; STEP pad → its locks and manual on/off edit; PATTERN pad → slot to default; **knob** → default value + its random amount cleared (in CYC EDIT: the cycle's override and random override; with steps selected: that lock on the selected steps). **[Rack]**                                                                                                                                                              |
| L17–L19 | Copy/Paste/Clear | light     |                        | COPY / CLEAR blink while armed. PASTE: green = clipboard holds a track, cycle or parameter, blue = a pattern; dim while holding, blinking while armed. The info line says what the armed button does.                                                                                                                                                                                                     |
| B36–B39 | Step page 1–4    | button    | `STEP_PAGE_PARAMS + n` | **STEP mode:** show steps 1–16 / 17–32 / 33–48 / 49–64 on the pads; pages beyond the track's Steps are disabled; selecting a page manually turns Follow off. Other modes: no function. |
| L36–L39 | Step page 1–4    | light     | `STEP_PAGE_LIGHTS + n` | Bright = page shown · dim = page exists · off = beyond Steps · blinking = playhead is on this page while another is shown. |
| B41     | Follow           | button    | `FOLLOW_PARAM`         | Toggle: pads (and STEP display page) follow the playhead's page. |
| B42–B57 | Track select 1–16 | small lit button | `TRACK_SELECT_PARAM + n` | Row above the pads, always active: click selects that track in any pad mode (this replaced the TRACK pad mode, B10, now retired). Ctrl+click adds / removes a track from the **group**, Shift+click adds the range from the selected track; a plain click selects one track and ends the group. Plain knob turns (and Harmony) set the selected track and move every grouped track by the same amount, each clamped to its range (list knobs: the same number of positions); CYC EDIT, step locks, Rnd assign amounts and page settings stay on the selected track. Grouped tracks light dim red; the header shows GROUP n n n. A track holding anything other than defaults (values, steps, pool, cycles, random, locks) lights dim orange. Light: white flash on the track's notes, red = selected (plus a red outline that stays while it flashes), blue-grey = muted. Their numbers also number the pads below. |
| L41     | Follow           | light     | `FOLLOW_LIGHT`         | |
| B20–B35 | Pad 1–16        | button    | `PAD_PARAMS + n`     | Meaning by mode.                                                                                                                                                                                                                                                  |
| L20–L35 | Pad 1–16        | RGB light | `PAD_LIGHTS + 3n`    | Colour conventions: orange = selected/pulse, flashing = playing, blue-grey = muted, red = edited cycle / retrigger.                                                                                                                    |

### 3.9 Inputs

Main panel (v0.2): Clock, Reset and Run (J01–J03, §3.1), and since v0.3 the NOTE IN jacks
V/Oct, Gate and Velocity with REC (J26–J28, B58; §3.15) under them in the TRANSPORT box, which
runs down beside the first knob row. Everything else — parameter
CV, Transpose, Phrase CV, Pattern select, Temp gate, Mute apply, mute gates, Random
regenerate — is on the IN expander (§3.12b). The old jacks J04, J10–J17, the Mod amounts
K23–K26 and lights L40–L43 are retired (their ids stay reserved in the module so saved
patches keep their indices). Main-panel Temp / All tracks / Random assign (B04–B06) sit in
the PERFORM box under TRANSPORT (the same width), beside GROOVE / TONAL.

### 3.10 Outputs

| ID  | Name           | Type             | Enum                | Behaviour                                                        |
| --- | -------------- | ---------------- | ------------------- | ---------------------------------------------------------------- |
| J20 | Pitch          | output (poly 16) | `PITCH_OUTPUT`    | One jack, channel n = track n. Lowest note of the track's chord. |
| J21 | Gate           | output (poly 16) | `GATE_OUTPUT`     | Includes repeats.                                                |
| J22 | Velocity       | output (poly 16) | `VELOCITY_OUTPUT` | 0–10 V, includes accent/ramp.                                   |
| J23 | Clock out      | output           | `CLOCK_OUTPUT`    | PPQN per menu.                                                   |
| J24 | Reset out      | output           | `RESET_OUTPUT`    |                                                                  |
| J25 | End of pattern | output           | `EOP_OUTPUT`      | Fires on pattern loop (longest track period, counted from the playing pattern's start) and on each pattern switch.                          |

### 3.11 Context menu

PPQN in/out · Run input mode · Mute apply timing (now / next beat / next bar / end of pattern) · On mute (finish / cut) · Display follows focus ·
MIDI output device (v0.2, §3.13).

### 3.12 Talea OUT expander (separate module, ~14 HP)

Placed directly right of Talea; data via Rack expander messages (double-buffered,
one-sample latency). Two columns × 8 tracks; each row: track number, status light, Pitch,
Gate, Vel.

| ID        | Name           | Type          | Enum                     | Behaviour                                                   |
| --------- | -------------- | ------------- | ------------------------ | ----------------------------------------------------------- |
| X-J01–16 | Track n Pitch  | output (poly) | `PITCH_OUTPUTS + n`    | Poly: one channel per chord note (≤ 8, after voicing, Range / Phrase, transpose and scale; notes the scale folds together count once). Mono (arp): one channel, the sounding note. |
| X-J17–32 | Track n Gate   | output (poly) | `GATE_OUTPUTS + n`     | Same channels; the chord shares the track's gate.          |
| X-J33–48 | Track n Vel    | output (poly) | `VELOCITY_OUTPUTS + n` | Per voice.                                                  |
| X-L01–16 | Track n status | RGB light     | `TRACK_LIGHTS + 3n`    | Dim orange, white on gate, blue-grey when muted; off when not attached. |
| X-L17     | Link           | light         | `LINK_LIGHT`           | Green when attached.                                        |

Rules: muted tracks/cycles gate 0 everywhere · no params on expander · pitch/vel held
between gates · channel count changes only on a new note.

### 3.12b Talea IN expander (v0.2, separate module)

Placed directly **left** of Talea (OUT stays right); data via Rack expander messages. Replaces
the main panel's I/O section.

- **One poly CV input per knob parameter**, each with an attenuverter: channel n = track n,
  a mono cable feeds every track (like Transpose). The CV adds on top of the final value
  (after cycle overrides, step locks and random): ±5 V × attenuverter = ± the parameter's
  full range. Steps, Division and Length aren't modulatable (they set the loop length, so
  modulating them would break track alignment, as with random).
- Moved here from the main panel: Transpose (poly V/oct), Phrase CV (poly), Pattern select
  (0–10 V), Temp gate, Mute apply.
- Performance inputs: poly mute gates (channel n = track n), Random regenerate trigger.

| ID          | Name                | Type          | Behaviour |
| ----------- | ------------------- | ------------- | --------- |
| Y-K01–15    | CV amount           | trimpot       | Attenuverter ±100 %, default 100 %. |
| Y-J01–15    | Parameter CV        | input (poly)  | Pulses, Rotate, Repeats, Ramp, Time, Pace, Velocity, Probability, Sustain, Accent, Timing, Delay, Pitch, Voicing, Range. ±5 V = ± full range; Rotate wraps; Repeats stays finite unless the knob is at ∞. |
| Y-J16       | Transpose           | input (poly)  | V/oct, scale-quantized (was J14). |
| Y-J17       | Phrase CV           | input (poly)  | Source when a track's Phrase = CV input (was J15). |
| Y-J18       | Pattern select      | input         | 0–10 V → 1–16 (was J16). |
| Y-J19       | Temp gate           | input         | Temp while high (was J04). |
| Y-J20       | Mute gates          | input (poly)  | A patched track follows its gate at each change: high = muted (pads still work in between). |
| Y-J21       | Mute apply          | input         | Trigger: applies the staged mutes (was J17). |
| Y-J22       | Random regenerate   | input (poly)  | Trigger: new random seed for the track (mono = every track). |
| Y-L01       | Link                | light         | Green when attached. |

**Main panel rework (done with the IN expander, v0.2):** the I/O section is removed (Mod 1–4 +
amounts, Transpose, Phrase CV, Pattern, Temp in); Data (K40) and Enter (B40) are removed;
the output section stays as it is; Temp / All tracks / Random assign move into the old I/O
area above the outputs; the context display grows to the full height freed under it.

### 3.12c v0.2 order

1. OUT expander (§3.12) — full chords per track; defines the expander messaging.
2. IN expander (§3.12b) + main panel rework.
3. Native MIDI out (§3.13).
4. CC tracks (§3.14) — values out both as a CV lane on the OUT expander and as MIDI CC.

### 3.13 Built-in MIDI out (v0.2)

v0.1 uses VCV Core CV-MIDI. v0.2: one `midi::Output` with the standard Rack driver/device
chooser (OS devices standalone; Rack's plugin/DAW driver under Rack Pro → one 16-channel
stream, DAW routes by channel). Per-track MIDI channel (SETUP page); full chords; note-off
tracking; all-notes-off on stop/reset/pattern change/removal. Port channel selector
hidden.

As built: device chosen in the context menu (*MIDI › MIDI output*: driver / device). SETUP
page: the selected track's channel (OFF / 1–16; default track n → channel n; tracks may
share a channel) and CLOCK. Notes follow the gate outputs exactly (Transpose, CV, chords:
Poly = every chord note, Mono = the sounding one; velocity = note velocity; muted tracks
send nothing): a note-on ends the track's previous notes first, a note-off ends them all, on
the channel they started on. Stop, Reset, pattern switches and removing the module end
sounding notes; *Kill stuck notes* also sends All Notes Off (CC 123) on all channels.
Switching the device or driver first ends the sounding notes on the old device; the audio
thread skips sending during the switch (no crash, no waiting). Optional MIDI clock (off by
default): 24 PPQN while playing, Start from the top / Continue
elsewhere, Stop; Reset while playing sends Start.

Audio setup: MIDI goes out as Rack processes each audio block, so keep an Audio module with a
device selected in any patch that sends MIDI, even with nothing patched into it (without one,
timing was audibly uneven in testing); block size sets the timing grain. If another program
holds an exclusive ASIO driver, don't route audio to an Audio module on that same driver: Rack's
whole engine stalls (Play and LFOs stop). Use WASAPI or another device for Rack.

---
### 3.14 CC tracks (v0.2)

SETUP page **TYPE** NOTE / CC per track (stored in the pattern, with copy / paste / clear /
undo); a CC track picks its number on **CC x10** / **CC x1** (0–119, default 1 = Mod wheel).
The rhythm is unchanged (steps, pulses, repeats, probability, timing, cycles, locks); each
pulse plays a **value** instead of a note:

- **Value = Velocity** (0–127; a CC value can reach 0), with step locks (the STEP lane reads
  VALUE), random, IN-expander CV, Accent and Ramp.
- **Glide = Sustain**: 0 jumps; otherwise the value slews to the new one over Sustain × the
  gap to the next pulse or repeat (100 % = the whole gap).
- **Outputs**: Pitch (main panel channel n and the OUT expander) carries the lane, 0–10 V
  (127 = 10 V); Gate still gates each pulse; Velocity carries the target value (no glide).
- **MIDI**: Control Change on the track's channel when the integer value changes (≤ ~1 kHz),
  no notes. A device switch resends the current values.
- The pitch settings (Pitch, pool, scale, Harmony, Voicing, Range, Phrase, Style) are unused:
  the PHRASE / SCALE / STYLE pages show dimmed.

### 3.15 SEQ mode (v0.3) **[Rack]**

A track — or one cycle of it — can be a plain step sequencer instead of a Euclidean one.
SETUP page **MODE** EUCLID / SEQ (in CYC EDIT **CYC MODE**: the edited cycle's own mode; also in
the context menu). Stored per track, per cycle and per pattern like everything else.

- **The sequence**: per step, a chord of up to 8 notes (none = a rest), a velocity (1–127)
  and a gate length in **steps** (1–64; more than one = tied over the next steps until they
  end, or until the next step with notes cuts them). Steps, Division and Length still set the
  loop, so cycles, loop sync, random per cycle and the pattern length work unchanged.
- **Not used in SEQ mode**: Pulses, Rotate, the Euclidean step edits; Scale, Root, Voicing /
  Style, Mono, Range / Phrase and Harmony (the PHRASE / SCALE / STYLE pages show dimmed; the
  Pulses, Rotate, Harmony, Voicing and Range knobs are dimmed and don't turn). The Velocity
  knob gives new notes (KEYS, recording without a VEL input) their velocity.
  Notes play as entered, **+ Pitch and Transpose** (no quantizing).
- **Still applies**: Velocity locks (over the step's velocity), random, IN-expander CV, Accent,
  Ramp, Probability, Repeats (with repeats the gate follows their spacing), Timing, Delay,
  Sustain (shapes the last step of the gate: 50 % = the whole step).
- **Switching to SEQ** with an empty sequence fills it from the hits playing now (each with
  the Euclid pool's notes and the Velocity knob, one step long), so the rhythm carries over.
- **Cycles** play the track's sequence until edited: the first edit in CYC EDIT (pads, KEYS,
  lanes, recording) copies the track's into the cycle, which then keeps its own. CYCLE page
  CLEAR **SEQ** (or ALL) puts the cycle back on the track's sequence; **MODE** drops its own mode.
- **Editing**: STEP pad mode click = selects just that step (green) for the KEYS page and the
  info line, with no locks; a rest stays a rest until KEYS gives it notes (a step's first note
  brings the Velocity knob, 1 step). Right-click = the step becomes a rest. Ctrl+click starts / changes the step **lock**
  selection as in EUCLID mode (blue; knobs turned lock those steps), a plain-clicked step
  joining it. The KEYS page edits the selected steps' chord (any key — the scale doesn't apply; **REST** clears them) and the STEP page shows
  **N.VEL** (velocity) and **GATE** (1–16 steps) lanes first, click / drag to set, right-click
  = default. SEQ mode has no pool: without a selection the KEYS page is empty. The KEYS
  page marks every note of the chord sounding now. COPY /
  PASTE / CLEAR on a step pad include its notes; on a track button its sequence.
- **Recording** (REC, B58; only in SEQ mode): notes at NOTE IN (poly V/Oct + Gate + Velocity,
  J26–J28) go into the selected track's sequence — the edited cycle's in CYC EDIT.
  - *Playing* = live: each note goes to the **nearest step** (quantized to the Division; past a
    loop's last step, the next loop's first), its length rounded to whole steps (min 1). Notes
    starting on a step while an earlier note there is held join its chord (loudest velocity,
    longest gate); a step played again on a later pass is replaced, or with **OVD** (B59)
    the new notes join its chord (loudest velocity, longest gate kept).
  - *Stopped* = step record: each chord goes to the **cursor** (blinking red pad; starts at the
    first selected step or step 1; a pad click moves it), gate 1 (with OVD: added to the
    step's notes, its gate kept); when all keys are released
    the cursor moves on, wrapping at Steps.
  - A take (REC on → off) is one undo step. REC goes off when the target leaves SEQ mode.
- **Monitoring**: in SEQ mode the keys held at NOTE IN always sound on the selected track's
  outputs (main jacks, OUT expander, MIDI), REC on or off, with the playback pitch path (+ Pitch
  and Transpose) and their velocity. While keys are held they take the track's outputs over
  from its sequence; keys added to a held chord don't retrigger the gate. Not on CC tracks.

## 4. Context display pages (D01)

Header: page name · track number/colour · cycle number (red if CYC EDIT) · step (if locking).

| Page    | Shows                                                                                                                                                   | Editable here                                                                                                                                 |
| ------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| TRACK   | Euclidean ring of selected track with playhead, generated vs manual hits, repeats ticks; overview of all 16 tracks as mini rings.                       | Track name/colour; track type (Note / CC / FX — §5).                                                                                        |
| STEP    | With several cycles (TRACK/STEP pages): the current 16-step page as an edit row (TRACK, or EDIT n in CYC EDIT) plus one lane per cycle showing that page as the cycle plays it (label white = playing, blue-grey = muted, * = has changes, box = selected, red in CYC EDIT; playhead on the playing lane). STEP page: the current 16-step page as an edit row (TRACK, or EDIT n in CYC EDIT; selection / lock marks) and a **lane per locked parameter** (up to 4; a + on the last label when more are locked) showing each step's lock as a bar (from the centre for bipolar parameters). Click or drag in a lane sets that step's lock (drawing across steps); right-click removes it. In CYC EDIT the lanes show and edit the edited cycle's locks over the track's. Later: retrigger markers. | Toggle pulses, add/remove locks, draw lock values across steps, retrigger, copy/clear step. SEQ mode: N.VEL / GATE lanes first (§3.15).                                                   |
| CYCLE   | Cycle count (1–8, default 1), slots (* = overrides, (n) = muted), selected and playing cycle, override count. | Count; select; mute; clear all overrides or one parameter of the selected cycle (SEQ / MODE: its own sequence / mode, §3.15). |
| KEYS    | Piano keyboard, 4 octaves (C2–B5 by default; < > shift an octave, C0 up to B8), C keys named. Pool notes orange, the sounding note a white dot, root marked, out-of-scale keys tinted. With steps selected (STEP mode, Ctrl+click): the first selected step's pool — blue = its own **step pool** lock, orange = the track's pool it would start from. Dimmed on a CC track. | Click a key = add / remove it from the pool (max 8, last note stays; out-of-scale keys can't be added, an out-of-scale pool note can be removed); in CYC EDIT the edited cycle's pool. With steps selected a click edits their **step pool** (starting from the track's pool; every selected step gets the result; in CYC EDIT the edited cycle's locks); **TRACK POOL** drops their step pools. SEQ mode (§3.15): any key; selected steps' chord, **REST** clears them. |
| SCALE   | SCALE (2 rows) and ROOT rows; NOTES = a one-octave keyboard (the KEYS page component, shorter): scale notes orange, root marked; click toggles a note (turns the scale into User).                                                                                         | Preset scale (list below), user scale editor (C-rooted, transposed by Root), Root C…B.                                                       |
| PHRASE  | Selected phrase shape drawn over Phrase Length, notes resulting from Range. **User**: the drawn grid instead — 16 columns across the phrase × 12 levels, the set cell bright with its stem below, the playing column lit; the corner shows the note it is reaching for. Click / drag a column to set its level. | Phrase: Cadence 1–4, Saw, Triangle, Sine, Pulse, CV input, **User** · Phrase length 1/2/4/8/16/32/64 bars (default 4), or loops · **SYNC** BARS / LOOP (the length row reads LOOPS when synced; the diagram then spans that many loops). Phrase, length, sync and the User grid are per track **and per cycle** (CYC EDIT edits the cycle's own copy, made from the track's on the first edit). |
| STYLE   | Chord/arp motion diagram for current pool + Voicing amount.                                                                                             | Style: Fixed, Ramp, Climb, Up/Down, Climb Up/Down · Poly / Mono (arp) · Direction up/down.                                                  |
| REPEAT  | One pulse's repeat burst: spacing (Time/Pace), velocity ramp.                                                                                           | Mode Choke / Tail · Division mode (fixed / triplet / free). (Time is set by K09 only.)                            |
| GROOVE  | Groove curve over Groove Length; per-step accent result; swing grid.                                                                                    | Groove: Agogo, Timbales, Congas, Bongo, Saw, Triangle, Sine, Pulse · Groove length 1–64 bars (default 4), or loops (waveforms only: the row is dimmed while a percussion groove is selected) · Timing subdivision ×1/×2/×4 · **SYNC** BARS / LOOP (see K14). |
| RANDOM  | Table of every randomisable parameter: amount (bipolar) and unit (click: per cycle / per Random Rate); C = per cycle, R = per Random Rate. SEQ row: REGEN (new seed), PHASE −/+ (shift every sequence one value), LOCK (freeze the sequences as they are now, Variance stops re-rolling; click again to unlock), PROB −/+ (shift Probability's skip pattern one step), SLEW (click steps 0/25/50/75/100 %: Random Rate values glide to the next over that much of each tick). CLEAR row (ALL / one parameter); info line shows Variance, Rate, phases, lock. Opens when Rnd assign turns on. | Amount, lock, slew, phase shift ±1, regenerate 16-step sequence, probability phase shift.                                                    |
| PATTERN | PLAY row: the 16 slots (orange = playing, * = holds a pattern, outline = queued / in chain), click = queue. COPY TO n: copies the playing pattern to slot n (while Temp is on the row reads TEMP TO and commits the Temp state there; onto the playing slot it keeps the Temp edits). CLEAR n: slot back to the default. SWITCH: switch timing (Now / Beat / Bar / Pattern end, default Pattern end; one setting per module). Info line: playing, queued with beats to go, chain. PATTERN mode opens this page.                                                                                                              | Chain edit, copy/clear pattern, commit Temp state to a slot.                                                                                  |
| SONG    | **Song mode**: a list of up to 64 entries, each a pattern slot played for a number of bars (1–999). Top row: SONG ON/OFF, LOOP, FOLLOW, < > pages of 16 entries. Grid (2 × 8 cells, "P5 x8"): click = select (blue outline), + = add the playing pattern, mouse wheel = bars; the playing entry shows its progress, the entry the song resumes at is outlined red. Rows: PATTERN (the selected entry's slot; its bars follow the new pattern's length), BARS (− / + / 1 2 4 8 16 32 / PAT = the pattern's length), EDIT (ADD the playing pattern, INSERT a copy before the selected entry, DELETE, CLEAR SONG). With the SONG page shown in PATTERN pad mode, pattern pads add their pattern to the song. Info line: entry, pattern, bar, total bars. | **Playback**: a new entry's bars = its pattern's length rounded up to whole bars. With SONG ON, Reset puts the song at entry 1 (Play starts it); turning it on mid-play starts entry 1 on the next bar line. Each entry plays its pattern for its bars, restarting the pattern from step 1 every time it ends, then the next entry starts on the bar line. After the last entry: LOOP = back to entry 1, otherwise the sequencer stops and rewinds to entry 1 (a Run gate has to go low before it runs again). Stop / Play continues where it stopped. **Override**: any pattern switch that isn't the song's (pad click, chain, CV) takes over; SONG ON blinks and the song remembers the bar it left. Clicking SONG resumes it there on the next bar line (dropping the queued switch / chain). The song and SONG ON / FOLLOW are saved with the module, not per pattern. |
| SETUP   | Pattern Quantize, MIDI channel, TYPE NOTE / CC, **MODE EUCLID / SEQ** (§3.15), track→track Output routing, Mod 1–4 routing matrix.                                                                   | Quantize (one per module, set on the PATTERN page for now), channel, routing, mod destinations/scope.                                                                             |

**Preset scales** (common sequencer set): Chromatic · Major (Ionian) · Natural Minor (Aeolian) ·
Harmonic Minor · Melodic Minor · Dorian · Phrygian · Lydian · Mixolydian · Locrian ·
Major Pentatonic · Minor Pentatonic · Blues · Major Blues · Whole Tone ·
Diminished (H-W) · Diminished (W-H) · Phrygian Dominant · Hungarian Minor · Double Harmonic ·
Japanese (In) · Hirajoshi · Arabic · plus **User** (custom). Stored as 12-bit masks so the
list is data, not code.

**Random direction semantics** are stored per
parameter: positive amount = CW behaviour, negative = CCW. E.g. Pulses CW adds / CCW
removes; Velocity CW varies per chord note / CCW equally; Scale CW random scale / CCW
random scale + root; Cycles CW skips next cycle / CCW repeats current.

---

## 5. Feature scope & roadmap

| Capability                                                                     | Talea                                                  | Version |
| ------------------------------------------------------------------------------ | ------------------------------------------------------- | ------- |
| 16 tracks, Euclidean Steps/Pulses/Rotate, Division, Length                     | K02–K06                                                | v0.1    |
| Custom rhythms (manual pulses), retrigger steps                                | STEP mode / page                                        | v0.1    |
| Per-step locks (pitch, velocity, sustain, accent, timing, repeats, tonal…)    | STEP mode + knobs / STEP page                           | v0.1    |
| Cycles (1–8, per-cycle overrides, cycle edit, cycle mute)                    | CYCLE mode/page, B16                                    | v0.1    |
| Note repeater (Repeats, Ramp, Time, Pace, Choke/Tail, ∞)                      | K07–K10, REPEAT page                                   | v0.1    |
| Velocity, Probability, Sustain, Accent+Groove, Timing+Delay                    | K11–K16, GROOVE page                                   | v0.1    |
| Note pool, Pitch, Harmony, Voicing+Style, Range+Phrase, Scale+Root, user scale | K17–K20, KEYS page                                     | v0.1    |
| Random (per-param amount, variance, rate, lock, slew, phase)                   | K21–K22, B06, RANDOM page                              | v0.1    |
| Patterns ×16, quantized switching, chaining                                   | PATTERN mode/page                                       | v0.1    |
| Mute (staged / instant), Temp (momentary / latched / all tracks / commit)      | B04, B05, MUTE mode                                     | v0.1    |
| Copy / paste / clear: track, pattern, parameter, step, cycle; multi-paste      | B17–B19                                                | v0.1    |
| Multi-track edit (relative), all-tracks edit                                   | Ctrl-click pads, B05                                    | v0.1    |
| Tempo 24–280, clock in/out, PPQN                                              | K01, J01, J23                                           | v0.1    |
| MIDI out, per-track channel                                                    | §3.13                                                  | v0.2    |
| **CC tracks** (track outputs a CC/value sequence)                        | SETUP › TYPE, §3.14 (CV lane + MIDI CC)                  | v0.2    |
| **FX tracks** + Hold mode (process incoming notes)                       | Track type FX + poly V/Oct & gate inputs (design in v0.3) | v0.3    |
| Track→track Output routing                                                    | SETUP page (mainly feeds FX tracks)                     | v0.3    |
| Banks (16 × 16 patterns), tempo per bank                                      | One bank (16 patterns) per module instance; save/load via Rack's standard module preset menu | v0.1 |
| Hardware-only concerns (wireless sync, companion config app, LED brightness)   | N/A in Rack                                             | —      |

---

## 6. Decisions & open questions

| # | Topic                     | Decision                                                                                                         |
| - | ------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| 1 | Panel width               | 40 HP — confirmed by first SVG draft (`docs/panel/`, generated by `tools/gen_panel.py`).                        |
| 2 | Knobs on selection change | Jump to stored value (track / cycle / step).                                                                     |
| 3 | Steps max                 | 64; raisable via one constant.                                                                                   |
| 4 | Root / scale              | Per track, and per cycle. Copy/paste incl. single-parameter copy.                                                |
| 5 | Chord output              | Main = lowest note; full chords on expander.                                                                     |
| 6 | Step editing              | Per-step lock model, plus STEP page lanes. Probability is a per-track knob and also lockable per step.           |
| 7 | MIDI out                  | Native in v0.2 (§3.13); CV-MIDI still works.                                                                     |
| 9 | CC tracks                 | v0.2. Output both as a CV lane on the OUT expander and as MIDI CC.                                               |
| 10 | FX tracks / Hold         | v0.3.                                                                                                            |
| 11 | Banks                     | Bank = project level = one module instance (16 patterns). Saved with the patch, and as a module preset via Rack's standard right-click *Preset → Save as / Load* (works automatically from `dataToJson`/`dataFromJson`). |
| 12 | Unspecified ranges        | Pulses default 0 · Repeats default 0 (no repeats) · Repeat Time = quantized note values incl. dotted/triplet, default 1/16 · Range 0–48 semitones · Pitch ±4 oct · Voicing 0–16 · Random Rate default 1/16 · standard scale list (§4). |

No open questions.

---

## 7. Data model notes

- **Track** (per pattern): base values for every parameter; type; note pool (≤ 8 notes);
  scale mask + root; selections (style, phrase, groove, modes, lengths); MIDI channel.
- **Cycles**: `cycles[8]` per track (8 leaves display room for a lane per cycle), each a *sparse* map param → override value + mute flag.
  Effective value = cycle override if present, else track base. Non-cycle-edit knob turns
  apply a relative delta to base and all overrides. Default count 1 (identical to more
  cycles without overrides). A track moves to its next cycle each time it completes a loop:
  its Length, or one pass of its Steps if Length is ∞. With one cycle an ∞ Length never
  restarts the track. Overrides cover the knob parameters and step edits: toggling a step in CYC EDIT gives
  the cycle its own manual/removed hits (copied from the track on the first edit); track
  step edits then no longer reach it. Scale / root / user scale / pool per cycle: in CYC EDIT the SCALE page, KEYS page and Harmony edit the cycle's own copy (made from the track's on the first edit); CYCLE page CLEAR › SCALE drops it.
- **Steps**: manual add/remove masks on the track (unrotated positions). **Step locks**
  (`StepLocks`, per track, per pattern): per unrotated step a sparse map param → value, so
  they move with Rotate. A pulse on a locked step (and its repeats) plays track value →
  cycle override → lock → random. Track locks (set outside CYC EDIT) play in every cycle;
  locks set in CYC EDIT belong to the edited cycle (`CycleLocks`, a list of up to 128
  step/param/value entries per cycle) and win over the track's on that step.
  Loop-shape parameters (Steps, Pulses, Rotate, Division, Length) can't lock. Pitch locks
  are quantized to the scale like the knob. Retrigger flag later.
- **Random**: per track: Variance, Random Rate, seed; per parameter: bipolar amount and unit
  (per cycle loop: Pulses, Rotate, Repeats, Repeat Time by default; the rest per Random
  Rate tick). Values are drawn from a seeded 16-value sequence as a pure function of the
  loop / tick count, so replays and seeks repeat them. Phase offsets the positions; Lock pins each sequence to the pass in force when locked (stored as the loop and tick counts); Slew interpolates Random Rate values within a tick; Probability phase offsets the skip pattern's step keys.
  Amounts can be overridden per cycle (Rnd assign in CYC EDIT; RANDOM page marks the
  cycle's own with *, CYCLE page CLEAR lists them as R.<param>). Cycle amounts are
  absolute: changing the track's amount doesn't move them. Variance, Rate, unit and seed
  stay per track. Like everything in `Pattern`, all of this is per pattern.
- **Temp**: a separate overlay struct; never written into pattern data unless committed.
- **Bank**: 16 `Pattern` slots in the engine; the playing one is the live, edited copy and
  is filed back into its slot on a switch. Tracks run on pattern time (host time minus the
  playing pattern's start), so a switch restarts them together. Mutes are per pattern.
  JSON saves only the slots that differ from the default, plus the playing slot and the
  switch timing; patches from before the bank load into slot 1.
- JSON stores a **format version** and only used steps/cycles/locks; missing values →
  defaults, so raising `kMaxSteps` or adding parameters keeps old patches loading.
- Copy/paste operates on these structs (track, cycle, step, single parameter).
- Step paging is derived from `kMaxSteps / 16`. At 64 steps it maps to the four page
  buttons B36–B39; if `kMaxSteps` is raised beyond 64, the page buttons become
  prev / next with a "page n/N" readout on the display.

---

## 8. Architecture (host-independent core)

**Decision:** the sequencer is written as a host-independent C++ core with thin adapters,
so it can later ship as a DAW plugin as well as a VCV Rack module.

```
src/core/      pure C++17 — no Rack, plugin or OS headers
  engine        sequencing: Euclid, cycles, repeats, groove, tonal, random
  model         tracks, patterns, cycles, step locks, scales
  params        single parameter table: ids, ranges, defaults, display names
  state         serialize / deserialize to a JSON string (format-versioned)
  view          display-page models: which fields a page shows / edits (no drawing)
src/rack/      VCV Rack adapter: Module, panel, NanoVG display, expander, MIDI out
src/plugin/    future: CLAP / VST3 / AU adapter + UI
tests/         core unit tests, built and run without Rack
```

### Core rules

1. **No voltages, no MIDI in the core.** The engine emits abstract events —
   note-on / note-off per track and voice, with note number, velocity and a precise
   time offset. Adapters translate (Rack: V/Oct, gate, velocity CV; plugin: MIDI).
2. **Musical time is supplied by the host.** The core advances in musical ticks. Rack
   feeds it from the clock input / internal tempo; a plugin feeds DAW transport
   position + tempo per block.
3. **Lookahead scheduling.** Negative Delay, swing and Pace can place events before the
   triggering clock tick. The core schedules events ahead with absolute musical
   timestamps; the Rack adapter predicts the next clock from the measured period.
4. **Own seeded PRNG** in the core (not Rack's), so locked random sequences are identical
   in every host.
5. **One parameter table** generates Rack `configParam` calls and plugin parameter lists.
6. **State = JSON string** owned by the core; Rack embeds it in the patch / preset,
   a plugin stores it in the project.
7. **Real-time safe.** No allocation or locks on the audio thread; UI → engine edits go
   through lock-free queues.
8. **"Selected track / cycle / step" is a UI concept** and never enters the engine. The
   engine only knows per-track (per-cycle, per-step) values. This keeps DAW automation
   possible: a plugin can expose fixed per-track parameters or a curated subset.
9. **Display logic separate from drawing.** Page models live in `core/view`; only rendering
   is host-specific (NanoVG in Rack; iPlug2 also offers NanoVG, so drawing may be shared).

### Licensing note

Rack plugins built on the Rack SDK are GPL-3.0 (unless commercially licensed from VCV). The
core is our own code and can be licensed separately for a plugin build. Check SDK terms
(CLAP: MIT; iPlug2: permissive; JUCE: AGPL/commercial; VST3: verify current terms) when the
plugin work starts.

### Future: DAW plugin set (not in scope now)

Many DAWs (notably Ableton Live) merge a plugin's multi-channel MIDI output into one stream.
Planned workaround: a **plugin set**:

- **Talea** — the sequencer plugin, publishing its per-track event streams.
- **Talea Receiver** — a lightweight MIDI plugin placed on an instrument track. It
  subscribes to one track (or MIDI channel) of a chosen Talea instance and outputs
  only that stream, so it can be routed to a single instrument.

Considerations to resolve then:

- **Transport between instances.** Plugins may be loaded in one process or sandboxed in
  separate processes (DAW-dependent), so use shared memory / IPC with an instance
  registry, not just an in-process singleton.
- **Timing.** The DAW's processing order between the two plugins isn't guaranteed. Events
  must be published ahead of time with absolute timestamps (transport position), and
  the receiver emits each one at the right sample offset. Rule 3 (lookahead) is a
  prerequisite.
- **Discovery UX.** The receiver needs an instance + track picker, and must handle the
  sequencer being removed, duplicated or renamed.
