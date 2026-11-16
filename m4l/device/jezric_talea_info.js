// Live's Info View text for every control of the Talea Sequencer, on the device and in the
// floating window alike: a title and a text each (annotation_name / annotation). Adapted
// from the Rack module's tooltips (src/rack/Tooltips.hpp) to what the control does in Live.
// build.py puts them on the controls; the script sets the boards' and diagrams' (which
// change with the page).
//
//   panel    the panel's own controls (the left column, track buttons, pads, the tabs)
//   pageTabs a page's own text: the tab row shows the one under the mouse (or the page
//            shown), as a live.tab has one text for all its tabs
//   dials    a dial per track parameter (engine keys); "dialNote" follows each
//   rows     the display's rows, by name (jezric_talea_display.js); "rowsDefault" for one
//            without its own (shouldn't happen)
//   device   DEVICE's own controls build.py places (Instance, Ahead, the status line)
//   boards   the board under a page's first row, by what it draws
//   diagrams the diagram band, by page
//   keyboard the floating window's keyboard and its row
//
// Strict JSON after "module.exports =", so build.py can read it.

module.exports = {
	"panel": {
		"on": ["On", "Turns the whole sequencer on or off. Off, it plays nothing: what's sounding ends on the next step. Switched on while Live plays, it starts at Live's next launch quantization boundary (Live's Quantization menu), as a clip launched then would. Saved with the set and automatable."],
		"window": ["Window", "Opens or closes the floating window: a copy of the whole panel with the display under it and a keyboard, which stays open when another track is chosen. Its size: WINDOW on the DEVICE page (the gear). Saved with the set: the window opens with it if it was open."],
		"windowWin": ["Window", "Closes this window (the device's window button opens it again)."],
		"tracks": ["Track Buttons", "Click: the track the dials, step pads and display pages edit. Ctrl+click: add the track to the group or take it out. Shift+click: group the range from the edit track. A dial turn moves every grouped track by the same amount; a page setting goes to every grouped track. Red = the edit track, dim red = grouped, white = playing a note, blue-grey = muted, dim orange = holds changes. While COPY, PASTE or CLEAR is armed: copies, pastes on or clears the track."],
		"pads": ["Step Pads", "Step mode: the edit track's 16 steps on the step page (orange = a hit, white = playing, dark = cut off by Length, a blue bar = locks). Click: a hit on or off. Ctrl+click: select steps for locks (the dials then lock their values there; LOCKS shows them). SEQ mode: click selects a step for KEYS; right-click or Alt+click makes it a rest. In Cycle edit they edit the edited cycle. Mute mode: a pad per track; click stages a mute change (Apply or Mute again applies), Ctrl+click mutes at once. Pattern mode: the 16 pattern slots; click queues a switch, Shift+click builds a chain, Ctrl+click switches at once."],
		"padMode": ["Pad Mode", "What the step pads do: Step (the edit track's steps), Mute (a pad per track) or Pattern (the 16 pattern slots). Choosing Mute again applies the staged mutes."],
		"apply": ["Apply", "Applies the staged mutes, at the next mute boundary (APPLY on the Mute view)."],
		"rec": ["REC", "SEQ mode: records the notes played into this MIDI track (arm it, or set its monitoring to In) into the edit track's sequence, the edited cycle's in Cycle edit. Playing: live, each note to the nearest step. Stopped: step record at the blinking pad. Off when the edit track isn't in SEQ mode."],
		"ovd": ["OVD", "While recording: notes played on a step that already has notes are added to them. Off (default): they replace them."],
		"stepPage": ["Step Page", "Which 16 steps the pads show (1-16 ... 49-64; a page past the track's Steps doesn't take). Choosing one turns Follow off."],
		"follow": ["Follow", "The pads' step page follows the playing step."],
		"temp": ["TEMP", "While on, every pattern edit is temporary. Click it off: the edits are reverted (on the boundary TEMP OFF sets on the DEVICE page; TEMP's frame blinks meanwhile, a click cancels). Ctrl+click off: they're kept. Mutes are never reverted. A pattern switch ends Temp, reverting. Mappable: a controller button can hold it."],
		"pages": ["Display Pages", "The display's pages: TRACK (the track's shape and steps, a lane per cycle), LOCKS (selected steps' locks), CYCLE (the track's cycles), KEYS (the note pool), SCALE, PHRASE (the contour Range follows), VOICING (how Voicing moves the chord), GROOVE (the Accent shape and swing), RANDOM (random amounts), PATTERN (pattern slots), SONG (song mode), SETUP (the track's type, repeats, mode and channel). A blue frame: RANDOM while the track has a random amount, LOCKS while steps are selected; a red one: CYCLE in Cycle edit."],
		"pagesWin": ["Display Pages", "The display's pages, as the device's but for KEYS (this window's keyboard is always there). With PAGE SYNC on (DEVICE page) the window and the device show the same page."],
		"gear": ["Device Settings", "The DEVICE page: the device's own settings, for every track: TEMP OFF, WINDOW, PAGE LOCK, PAGE SYNC, Instance, Look-ahead and the status line."]
	},
	"pageTabs": {
		"TRACK": ["TRACK Page", "The edit track at a glance: its type, steps, pulses, rotation, division and length, and its steps, 16 to a row. With several cycles, a lane per cycle as it plays, under the step page the pads edit. With more than 16 steps, PAGE and FOLLOW for the pads' step page."],
		"LOCKS": ["LOCKS Page", "Step locks: a value of a parameter on chosen steps only. Select steps (Ctrl+click on the pads, or the STEPS row here), then turn a dial to lock it there. A lane per locked parameter shows each step's lock; click or drag to set it, Ctrl+click to remove it. SELECT ends the selection, takes the whole page or clears the locks. In Cycle edit the locks are the edited cycle's."],
		"CYCLE": ["CYCLE Page", "A track's cycles: up to 8 passes of its loop played in turn, each able to change the track (its own dial values, steps, locks, scale, pool, phrase, mode and random amounts). COUNT sets how many, CYCLE which one is edited, EDIT turns Cycle edit on (the dials and pads then edit that cycle, which plays alone), MUTE silences it, CLEAR drops its changes."],
		"KEYS": ["KEYS Page", "The note pool: the notes the track's pulses play, as a keyboard. Click a key to add or remove it (keys in the scale or pool). With steps selected: their own pool, in place of the track's (TRACK POOL drops it). SEQ mode has no pool: the keys edit the selected step's chord (REST makes it a rest)."],
		"SCALE": ["SCALE Page", "The scale and root the pool, Pitch and Range snap to, and the scale's notes (click one to make it a User scale). In SEQ mode the sequence follows a scale change. In Cycle edit, the edited cycle's own."],
		"PHRASE": ["PHRASE Page", "The phrase: the contour Range follows over time, adding notes above the pool. Its shape (cadences, waveforms, or USER: drawn here), its length in bars or loops, and whether it runs on pattern time or restarts with the track's loop."],
		"VOICING": ["VOICING Page", "How the Voicing dial moves the chord: the Style's path through the Voicing positions over successive notes, chords (POLY) or one note at a time (MONO), and whether Voicing takes the lowest note up or the highest down. The next notes drawn below."],
		"GROOVE": ["GROOVE Page", "The Accent shape (percussion patterns or waveforms), its length and sync, and the subdivision the Timing dial swings. The shape drawn below."],
		"RANDOM": ["RANDOM Page", "Random amounts: how far each parameter's random value moves it, and whether it changes at the random Rate (R) or once per cycle (C). Then the track's random sequence: Variance, Rate, Slew, Lock, and REGEN, PHASE, PROB and CLEAR ALL. In Cycle edit the amounts are the edited cycle's."],
		"PATTERN": ["PATTERN Page", "The 16 pattern slots: PLAY queues one (it switches at the SWITCH timing), COPY TO copies the playing pattern into a slot (TEMP TO while Temp is on), CLEAR resets one. The pads in Pattern mode do the same."],
		"SONG": ["SONG Page", "Song mode: patterns in order, each for a number of bars, on Live's timeline (its bar 1 is Live's bar 1). SONG and LOOP turn it on and loop it; the entries are edited here (PATTERN, BARS, ADD, INSERT, DELETE). A pattern switch of yours takes over until BACK TO SONG."],
		"SETUP": ["SETUP Page", "The edit track's setup: NOTE or CC, its CC number, Choke or Tail repeats, EUCLID or SEQ mode (the edited cycle's own in Cycle edit), and its channel: the Track Receivers set to it play the track."],
		"DEVICE": ["DEVICE Page", "The device's own settings, for every track: TEMP OFF, WINDOW, PAGE LOCK, PAGE SYNC, Instance, Look-ahead and the status line."],
		"MUTE": ["Mute View", "Mute pad mode: when staged mutes apply (APPLY) and whether a mute cuts what's sounding (ON MUTE). Choose a page tab to leave Mute mode."]
	},
	"dialNote": " With steps selected (LOCKS) it sets their lock, where it can be locked; in Cycle edit the edited cycle's override. While COPY or CLEAR is armed, a click copies or clears its value.",
	"dials": {
		"steps": "Number of steps in the track's loop (1-64).",
		"pulses": "Euclidean hits, spread as evenly as possible over the steps. 0 = silent.",
		"rotate": "Shifts the whole pattern along the steps, hits set by hand included.",
		"division": "Length of each step, as a note value (triplets and free included).",
		"length": "Restarts the track after this long, whatever Steps is. Infinite: Steps alone sets the loop. Steps it cuts off are dark on the pads.",
		"repeats": "Extra notes after each pulse. The last position: until the next pulse. Choke or Tail: SETUP's REPEATS.",
		"ramp": "Velocity across the repeats. Right: builds up to Velocity. Left: fades down from it.",
		"repeatTime": "Spacing between repeats, as a note value.",
		"pace": "Right: the repeats slow down. Left: they speed up. Centre: even.",
		"velocity": "Base velocity of the track's notes (0-127). On a CC track, the value sent.",
		"probability": "Chance notes play. Right: any note can skip, repeats included. Left: pulses only (with their repeats). The same notes skip on every pass.",
		"sustain": "Note length: 50 % = one step, 100 % = two (notes overlap and retrigger).",
		"accent": "Velocity variation around Velocity, shaped by the groove chosen on the GROOVE page.",
		"timing": "Swing: every second step later (right) or earlier (left). Its subdivision: GROOVE's SWING.",
		"delay": "Moves the whole track later (right) or earlier (left), up to one step.",
		"pitch": "Transposes the note pool in semitones (up to 4 octaves each way), snapped to the scale.",
		"voicing": "How far the Style moves the chord: each position takes the lowest note up an octave (or the highest down). Style: the VOICING page.",
		"range": "Adds up to this many semitones above the pool, following the Phrase over its length. Phrase: the PHRASE page."
	},
	"rows": {
		"trackInfo": ["Track", "The edit track (or the edited cycle): type, steps, pulses, rotation, division and length."],
		"trackStepPage": ["Step Page", "Which 16 steps the pads and the board show (more than 16 steps). Choosing one turns Follow off."],
		"trackFollow": ["Follow", "The step page follows the playing step (the transport's Follow too)."],
		"lockSelect": ["Select", "NONE: ends the step selection. PAGE: selects the page's steps. CLEAR LOCKS: removes the selected steps' locks."],
		"lockStepPage": ["Step Page", "Which 16 steps the lanes show (more than 16 steps). Choosing one turns Follow off."],
		"lanePage": ["Lanes", "More than 6 parameters locked: which 6 lanes show."],
		"cycleCount": ["Cycles", "How many cycles the track has. Each plays one pass of the track's loop, in turn."],
		"cycleSelect": ["Cycle", "The cycle Cycle edit and this page work on (past the last adds cycles). 3* = has changes of its own, (3) = muted; the bar under it: playing. While COPY, PASTE or CLEAR is armed: copies, pastes on or clears the cycle."],
		"cycleEdit": ["Cycle Edit", "On: the dials and step pads edit the selected cycle, and only that cycle plays; LOCKS, SCALE, KEYS, PHRASE and RANDOM edit its own locks, scale, pool, phrase and random amounts. Off: they edit the track. The CYCLE tab's red frame blinks while it's on."],
		"cycleMute": ["Cycle Mute", "Mutes the selected cycle: the track is silent while it plays."],
		"cycleClear": ["Cycle Clear", "Drops the selected cycle's own changes: ALL, or one item (HITS = step edits, LOCKS, SCALE, SEQ, MODE, a parameter, R.x = a random amount)."],
		"cyclePlaying": ["Cycles", "Which cycle plays, or what Cycle edit does."],
		"keysOctave": ["Octave", "Shifts the keyboard an octave."],
		"keysHelp": ["Keys", "What a key click does now."],
		"seqRest": ["Rest", "Makes the selected steps rests (removes their notes)."],
		"trackPool": ["Track Pool", "Drops the selected steps' own pools: they play the track's pool again."],
		"scale": ["Scale", "The scale the pool, Pitch and Range snap to. Editing its notes (the keys below) makes it the User scale. SEQ mode: the keys, the notes recorded and the notes played snap to it, and changing it moves the sequence's notes onto it (nearest note, ties up)."],
		"root": ["Root", "The scale's root note. SEQ mode: the sequence's notes move onto the new scale."],
		"phrase": ["Phrase", "The contour Range follows: CAD 1-4, waveforms or USER (draw it below)."],
		"phraseLength": ["Phrase Length", "Phrase length, in bars (LOOPS: in loops of the track, while SYNC is LOOP)."],
		"phraseLoop": ["Phrase Sync", "BARS: the phrase runs on pattern time. LOOP: it restarts with the track's step loop."],
		"style": ["Style", "The path through the Voicing positions over successive notes."],
		"mono": ["Play", "POLY: chords. MONO: arpeggiates the voiced pool, one note per event."],
		"voicingDown": ["Voice", "UP: Voicing takes the lowest note up an octave. DOWN: the highest down."],
		"groove": ["Groove", "The Accent shape: percussion patterns (one bar) or waveforms (over the length)."],
		"grooveLength": ["Groove Length", "Waveform grooves' length, in bars (LOOPS: in loops of the track, while SYNC is LOOP). Percussion grooves are one bar."],
		"swingSubdivision": ["Swing", "Timing's subdivision: every second unit of 1, 2 or 4 steps moves."],
		"grooveLoop": ["Groove Sync", "BARS: grooves run on pattern time. LOOP: they restart with the track's step loop."],
		"rnd_*": ["Random Amount", "How far the parameter's random value moves it, -100 to +100 % (0: none). In Cycle edit, the edited cycle's own. A blue frame on the RANDOM tab while the track has any."],
		"rndc_*": ["Random Timing", "When the parameter's random value changes: R = at the random Rate, C = once per cycle loop."],
		"rndVariance": ["Variance", "Chance each of the track's random values is re-rolled when the sequence comes round (0: the same values forever, 100 %: new every pass)."],
		"rndRate": ["Random Rate", "How fast the track's random values advance."],
		"rndSlew": ["Slew", "Glides between random values instead of jumping."],
		"rndLock": ["Random Lock", "Freezes the random values where they are."],
		"rndSeq": ["Random Sequence", "REGEN: new random values. PHASE - / +: shift the random sequence. PROB - / +: shift which notes Probability skips. CLEAR ALL: every random amount to 0 (in Cycle edit, the cycle's)."],
		"patternPlay": ["Play", "Queues a pattern slot: it switches at the SWITCH timing. 3* = holds a pattern."],
		"patternCopy": ["Copy To", "Copies the playing pattern into a slot. While Temp is on (TEMP TO): with Temp's edits; onto the playing slot it keeps them."],
		"patternClear": ["Clear", "Resets a pattern slot to the default (silent) pattern."],
		"patternQuantize": ["Switch", "When a queued pattern switch happens: at once, on the next beat, bar or pattern end."],
		"songOn": ["Song", "Song mode: the song plays its entries in order on Live's timeline (its bar 1 is Live's bar 1), each for its bars."],
		"songLoop": ["Song Loop", "Loops the song from the start when it ends; off, past the end the sequencer is silent."],
		"songResume": ["Back To Song", "A pattern switch of yours took over from the song: back to the song, where Live's timeline is."],
		"songPage": ["Song Page", "Which 16 song entries show."],
		"songSelect": ["Entries", "The song's entries: each one's pattern. Click selects it (PATTERN and BARS edit it); + after the last adds the playing pattern. The bar above: playing."],
		"songCount": ["Entries", "The song's entries: each one's bars. Click selects it (PATTERN and BARS edit it)."],
		"songSlot": ["Pattern", "The pattern the selected entry plays."],
		"songBars": ["Bars", "How long the selected entry plays, in bars."],
		"songBarsSet": ["Bars", "Sets the selected entry's bars: a preset, or PAT = the pattern's own length."],
		"songEdit": ["Song Edit", "ADD: append the playing pattern. INSERT: a new entry before the selected one. DELETE: remove the selected entry. CLEAR SONG: remove them all."],
		"songInfo": ["Song", "Where the song is: the entry, its pattern and bar."],
		"cc": ["Type", "NOTE: the track plays notes. CC: each pulse sends a value (Velocity) as MIDI CC on the track's channel."],
		"ccNumber": ["CC Number", "The CC number (0-119) a CC track sends."],
		"repeatTail": ["Repeats", "CHOKE: the next pulse cuts the repeats. TAIL: bursts overlap."],
		"seqMode": ["Mode", "EUCLID: pulses, rotation and a note pool. SEQ: a step sequencer, each step its own notes, velocity and gate. CYC MODE in Cycle edit: the edited cycle's own (CYCLE's CLEAR > MODE drops it)."],
		"channel": ["Channel", "The track's channel (1-16): the Track Receivers set to it play the track. Several tracks can share one."],
		"tempTiming": ["Temp Off", "When TEMP's revert lands while playing: at once, on the next beat, bar or pattern end (default bar). Keeping, and reverting while stopped, are at once."],
		"winZoom": ["Window Size", "The floating window's size: Live's zoom doesn't reach it, so set it to match."],
		"pageLock": ["Page Lock", "On (default): a dial turned leaves the page as it is. Off: it shows its page (Rack's \"display follows knobs\")."],
		"pageSync": ["Page Sync", "On (default): the device and the floating window show the same page. Off: each its own. Turned on in one, the other takes its page."],
		"muteTiming": ["Mute Apply", "When staged mute changes apply: at once, on the next beat, bar or pattern end."],
		"muteCuts": ["On Mute", "FINISH: a sounding note and its repeats play out. CUT: they stop at once."]
	},
	"rowsDefault": ["Talea", ""],
	"device": {
		"instance": ["Instance", "Which Talea Sequencer the Track Receivers follow (A-D): up to four in a set."],
		"lookahead": ["Look-ahead", "How many steps early the sequencer sends each step to the receivers (default 4). More gives Live's interface more room before a step is late."],
		"status": ["Status", "The sequencer's state: the instance, the receivers heard and any problem."]
	},
	"boards": {
		"steps": ["Steps", "The edit track's steps, 16 to a row: orange = a hit, white = playing, a blue bar = locks, blue = selected, faint = cut off by Length."],
		"cycles": ["Cycle Lanes", "The step page as the pads edit it (TRACK, or EDIT n in Cycle edit), then a lane per cycle as it plays (C2* plays differently; white = playing, blue-grey = muted). In Cycle edit, click a lane's label to edit that cycle."],
		"lanes": ["Lanes", "The STEPS row: click selects a step, Ctrl+click adds one. Then a lane per locked parameter: click or drag sets the selected steps' lock, Ctrl+click removes it."],
		"keys": ["Keys", "Click a key: adds or removes the note in the track's pool (keys in the scale or pool; the others dimmed). With steps selected: their own pool (blue). SEQ mode: the selected step's chord."]
	},
	"diagrams": {
		"SCALE": ["Scale Notes", "Click a note: adds or removes it from the scale (it becomes the User scale)."],
		"PHRASE": ["Phrase", "The contour Range follows over the phrase. USER phrase: click or drag draws it."],
		"VOICING": ["Voicing", "The next notes as Style and Voicing play them."],
		"GROOVE": ["Groove", "The Accent shape over its span, the bright part scaled by Accent."]
	},
	"keyboard": {
		"keys": ["Keyboard", "Always here, as KEYS: click a key to add or remove the note in the track's pool (keys in the scale or pool; the others dimmed). With steps selected: their own pool (blue). SEQ mode: the selected step's chord."],
		"octave": ["Octave", "Shifts the keyboard an octave down or up (saved with the set)."],
		"help": ["Keys", "What a key click does now."],
		"rest": ["Rest", "Makes the selected steps rests (removes their notes)."],
		"pool": ["Track Pool", "Drops the selected steps' own pools: they play the track's pool again."]
	}
}
