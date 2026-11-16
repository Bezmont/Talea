// The sequencer's display: its pages and each page's rows, for the patch builder
// (device/build.py builds the controls from it) and the script (jezric_talea.js shows a
// page's controls, sets them from the engine and takes their edits). One row each:
//
//   name     the control's name: its scripting name is "d_<name>", its label's "dl_<name>"
//   label    the row's label, as on the Rack module's display ("alt": the label while the
//            row's "altWhen" setting is on, e.g. LOOPS while SYNC is LOOP)
//   kind     "tab" (a live.tab), "menu" (a live.menu), "buttons" (a live.tab in button
//            mode: each click an action, nothing stays selected), "num" (a live.numbox),
//            "text" (a line the script writes: "text" says which), "button" (one button,
//            a live.text: "text" its label)
//   items    the choices; "@scales" is the engine's scale list (Tonal.hpp), "@randomRates"
//            its Random Rate names (Values.hpp)
//   values   the engine value of each choice, if not 0, 1, 2 ...
//   field    a track setting (jezric_talea_core.js FIELDS), for the edit track
//   setting  one of the sequencer's own settings (jezric_talea_core.js SETTINGS)
//   action   what a click does ("queue", "copy", "clear": pattern slots)
//   mods     the modifier keys are read before a click ("padMods", as for a pad press):
//            CYCLE's cells, where Ctrl+click on a PASTE target keeps PASTE armed
//   min, max a numbox's range
//   width    the control's width in px (default: the whole row)
//   activeWhen  dimmed unless that field's value is in the list ("@songEntry": unless a
//            song entry is selected)
//   random   one of the edit track's random settings (jezric_talea_core.js RANDOM), of
//            parameter "param" for "amount" and "perCycle"; "randomAction": a buttons
//            row's RANDOM button numbers (talea_random_action)
//   x, row   where it goes: x px from the display's left edge, row number (default: the
//            next row, at the left); "labelW" the label's width ("label" left out: none);
//            "anchor": "right" keeps it as far from the right edge in a wider display (the
//            floating window's)
//   kind     "toggle": an on/off button labelled "text" (two buttons, the script showing the
//            one for the state; a click flips it; not a parameter)
//   unit     "%": a numbox shows its value in percent
//   fontsize a tab row's font size (default 8)
//   showWhen shown only while the script says so ("lanePages": more than 6 lanes,
//            "stepPages": more than 16 steps, "seqStep": SEQ mode with steps selected, "stepPool": the first
//            selected step has its own pool (EUCLID mode),
//            "songOverridden": a pattern switch took over from the song, "songPages":
//            more than 16 song entries)
//   seqUnused dimmed while the edit track is in SEQ mode (it doesn't use it)
//   altWhen  "@cycleEdit": the alt label in Cycle edit; "@temp": while Temp is on
//   playBars a tab row with a bar under each choice, shown under the one playing
//            ("d_<name>_play<i>": CYCLE's playing cycle); "above": a thicker blue bar in
//            the gap above the row instead (SONG's playing entry, between its rows)
//   fit      a renamed row ("marks") only as wide as its choices need (each as wide as the
//            longest's text and some padding), not the whole row: set by the script
//   lines    a tab row's choices on that many lines
//   dy       moves the row up or down (px): SONG's bars row sits right under its patterns
//   marks    "used": a pattern slot's choice reads "3*" while the slot holds a pattern;
//            "cycles": a cycle's "3*" with changes, "(3)" muted; "cycleClear": the choices
//            are what the selected cycle changes (ALL, HITS, the overridden parameters);
//            "songEntries" / "songCounts": SONG's entries, two tabs one over the other: each
//            entry's pattern on top ("P3"), its bars under it ("x4"), "+" after the last;
//            "songPages": as many SONG pages as there are entries for
//
// "dialPages": the page a dial's turn shows (Rack's "display follows knobs"), while DEVICE's
// PAGE LOCK is off.
// "diagrams": the pages with a diagram in the band under their rows
// (jezric_talea_diagram.js), and which; "boards": the pages drawn under their first row
// (the same script, a bigger box).
//
// "MUTE" isn't a page: Mute pad mode shows it over whatever page is chosen, as on Rack.
// "DEVICE" isn't a tab either: the gear right of the tabs shows it ("gear"), the device's
// own settings (for every track: TEMP OFF, then Instance, Ahead and the status line, placed
// by build.py).
// LOCKS is Rack's STEP page (its step locks). No REPEAT page: its one setting, Choke or Tail,
// is on SETUP. VOICING is Rack's STYLE page
// (what the Voicing dial moves). Pages without rows aren't built yet; they show a note.
//
// Strict JSON after "module.exports =", so build.py can read it.

module.exports = {
	"diagrams": {"GROOVE": "groove", "PHRASE": "phrase", "VOICING": "style", "SCALE": "scale"},
	"boards": {"TRACK": "steps", "KEYS": "keys", "LOCKS": "lanes"},
	"dialPages": {
		"steps": "TRACK", "pulses": "TRACK", "rotate": "TRACK", "division": "TRACK", "length": "TRACK",
		"repeats": "SETUP", "ramp": "SETUP", "repeatTime": "SETUP", "pace": "SETUP",
		"velocity": "GROOVE", "probability": "GROOVE", "sustain": "GROOVE", "accent": "GROOVE", "timing": "GROOVE", "delay": "GROOVE",
		"pitch": "SCALE", "voicing": "VOICING", "range": "PHRASE"
	},
	"gear": "DEVICE",
	"pages": ["TRACK", "LOCKS", "CYCLE", "KEYS", "SCALE", "PHRASE", "VOICING", "GROOVE", "RANDOM", "PATTERN", "SONG", "SETUP"],
	"views": {
		"TRACK": [
			{"name": "trackInfo", "kind": "text", "text": "trackInfo", "labelW": 0},
			{"name": "trackStepPage", "label": "PAGE", "kind": "tab", "items": ["1-16", "17-32", "33-48", "49-64"], "action": "stepPage", "x": 278, "row": 0, "labelW": 0, "width": 116, "anchor": "right",
				"marks": "stepPages", "showWhen": "stepPages"},
			{"name": "trackFollow", "kind": "toggle", "text": "FOLLOW", "action": "follow", "x": 398, "row": 0, "labelW": 0, "width": 56, "anchor": "right",
				"showWhen": "stepPages"}
		],
		"LOCKS": [
			{"name": "lockSelect", "label": "SELECT", "kind": "buttons", "items": ["NONE", "PAGE", "CLEAR LOCKS"], "action": "lockSelect", "width": 180},
			{"name": "lockStepPage", "kind": "tab", "items": ["1-16", "17-32", "33-48", "49-64"], "action": "stepPage", "x": 244, "row": 0, "labelW": 0, "width": 116,
				"marks": "stepPages", "showWhen": "stepPages"},
			{"name": "lanePage", "kind": "tab", "items": ["1-6", "7-12", "13"], "action": "lanePage", "x": 364, "row": 0, "labelW": 0, "width": 90,
				"marks": "lanePages", "showWhen": "lanePages"}
		],
		"CYCLE": [
			{"name": "cycleCount", "label": "COUNT", "kind": "tab", "items": ["1", "2", "3", "4", "5", "6", "7", "8"], "action": "cycleCount"},
			{"name": "cycleSelect", "label": "CYCLE", "kind": "tab", "items": ["1", "2", "3", "4", "5", "6", "7", "8"], "action": "cycleSelect", "mods": true, "marks": "cycles", "playBars": true},
			{"name": "cycleEdit", "label": "EDIT", "kind": "tab", "items": ["OFF", "ON"], "action": "cycleEdit", "width": 60},
			{"name": "cycleMute", "label": "MUTE", "kind": "tab", "items": ["OFF", "ON"], "action": "cycleMute", "width": 60},
			{"name": "cycleClear", "label": "CLEAR", "kind": "buttons", "items": ["ALL", "HITS"], "action": "cycleClear", "marks": "cycleClear", "fit": true},
			{"name": "cyclePlaying", "kind": "text", "text": "cyclePlaying", "labelW": 0}
		],
		"KEYS": [
			{"name": "keysOctave", "label": "OCTAVE", "kind": "buttons", "items": ["<", ">"], "action": "octave", "width": 50},
			{"name": "keysHelp", "kind": "text", "text": "keysHelp", "x": 120, "row": 0, "labelW": 0, "width": 270},
			{"name": "seqRest", "kind": "button", "text": "REST", "action": "seqRest", "x": 400, "row": 0, "labelW": 0, "width": 54, "showWhen": "seqStep"},
			{"name": "trackPool", "kind": "button", "text": "TRACK POOL", "action": "trackPool", "x": 392, "row": 0, "labelW": 0, "width": 70, "showWhen": "stepPool"}
		],
		"SCALE": [
			{"name": "scale", "label": "SCALE", "kind": "menu", "items": "@scales", "field": "scale", "width": 180},
			{"name": "root", "label": "ROOT", "kind": "tab", "items": ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"], "field": "root"}
		],
		"PHRASE": [
			{"name": "phrase", "label": "PHRASE", "kind": "tab", "items": ["CAD 1", "CAD 2", "CAD 3", "CAD 4", "SAW", "TRI", "SINE", "PULSE", "USER"],
				"values": [0, 1, 2, 3, 4, 5, 6, 7, 9], "field": "phrase", "seqUnused": true},
			{"name": "phraseLength", "label": "BARS", "alt": "LOOPS", "altWhen": "phraseLoop", "kind": "tab",
				"items": ["1", "2", "4", "8", "16", "32", "64"], "field": "phraseLength", "seqUnused": true, "width": 240},
			{"name": "phraseLoop", "label": "SYNC", "kind": "tab", "items": ["BARS", "LOOP"], "field": "phraseLoop", "seqUnused": true, "width": 100}
		],
		"VOICING": [
			{"name": "style", "label": "STYLE", "kind": "tab", "items": ["FIXED", "RAMP", "CLIMB", "UP/DN", "CLB U/D"], "field": "style", "seqUnused": true, "width": 260},
			{"name": "mono", "label": "PLAY", "kind": "tab", "items": ["POLY", "MONO"], "field": "mono", "seqUnused": true, "width": 100},
			{"name": "voicingDown", "label": "VOICE", "kind": "tab", "items": ["UP", "DOWN"], "field": "voicingDown", "seqUnused": true, "width": 100}
		],
		"GROOVE": [
			{"name": "groove", "label": "GROOVE", "kind": "tab", "items": ["AGOGO", "TIMBAL", "CONGAS", "BONGO", "SAW", "TRI", "SINE", "PULSE"], "field": "groove"},
			{"name": "grooveLength", "label": "BARS", "alt": "LOOPS", "altWhen": "grooveLoop", "kind": "tab",
				"items": ["1", "2", "4", "8", "16", "32", "64"], "field": "grooveLength", "width": 240,
				"activeWhen": {"field": "groove", "in": [4, 5, 6, 7]}},
			{"name": "swingSubdivision", "label": "SWING", "kind": "tab", "items": ["x1", "x2", "x4"], "field": "swingSubdivision", "width": 120},
			{"name": "grooveLoop", "label": "SYNC", "kind": "tab", "items": ["BARS", "LOOP"], "field": "grooveLoop", "width": 100}
		],
		"RANDOM": [
			{"name": "rnd_pulses", "label": "PULSE", "kind": "num", "min": -100, "max": 100, "unit": "%", "random": "amount", "param": "pulses", "x": 0, "row": 0, "labelW": 30, "width": 34},
			{"name": "rndc_pulses", "kind": "tab", "items": ["R", "C"], "random": "perCycle", "param": "pulses", "x": 66, "row": 0, "labelW": 0, "width": 24, "fontsize": 9},
			{"name": "rnd_rotate", "label": "ROT", "kind": "num", "min": -100, "max": 100, "unit": "%", "random": "amount", "param": "rotate", "x": 91, "row": 0, "labelW": 30, "width": 34},
			{"name": "rndc_rotate", "kind": "tab", "items": ["R", "C"], "random": "perCycle", "param": "rotate", "x": 157, "row": 0, "labelW": 0, "width": 24, "fontsize": 9},
			{"name": "rnd_repeats", "label": "REP", "kind": "num", "min": -100, "max": 100, "unit": "%", "random": "amount", "param": "repeats", "x": 182, "row": 0, "labelW": 30, "width": 34},
			{"name": "rndc_repeats", "kind": "tab", "items": ["R", "C"], "random": "perCycle", "param": "repeats", "x": 248, "row": 0, "labelW": 0, "width": 24, "fontsize": 9},
			{"name": "rnd_ramp", "label": "RAMP", "kind": "num", "min": -100, "max": 100, "unit": "%", "random": "amount", "param": "ramp", "x": 273, "row": 0, "labelW": 30, "width": 34},
			{"name": "rndc_ramp", "kind": "tab", "items": ["R", "C"], "random": "perCycle", "param": "ramp", "x": 339, "row": 0, "labelW": 0, "width": 24, "fontsize": 9},
			{"name": "rnd_repeatTime", "label": "TIME", "kind": "num", "min": -100, "max": 100, "unit": "%", "random": "amount", "param": "repeatTime", "x": 364, "row": 0, "labelW": 30, "width": 34},
			{"name": "rndc_repeatTime", "kind": "tab", "items": ["R", "C"], "random": "perCycle", "param": "repeatTime", "x": 430, "row": 0, "labelW": 0, "width": 24, "fontsize": 9},
			{"name": "rnd_pace", "label": "PACE", "kind": "num", "min": -100, "max": 100, "unit": "%", "random": "amount", "param": "pace", "x": 0, "row": 1, "labelW": 30, "width": 34},
			{"name": "rndc_pace", "kind": "tab", "items": ["R", "C"], "random": "perCycle", "param": "pace", "x": 66, "row": 1, "labelW": 0, "width": 24, "fontsize": 9},
			{"name": "rnd_velocity", "label": "VEL", "kind": "num", "min": -100, "max": 100, "unit": "%", "random": "amount", "param": "velocity", "x": 91, "row": 1, "labelW": 30, "width": 34},
			{"name": "rndc_velocity", "kind": "tab", "items": ["R", "C"], "random": "perCycle", "param": "velocity", "x": 157, "row": 1, "labelW": 0, "width": 24, "fontsize": 9},
			{"name": "rnd_probability", "label": "PROB", "kind": "num", "min": -100, "max": 100, "unit": "%", "random": "amount", "param": "probability", "x": 182, "row": 1, "labelW": 30, "width": 34},
			{"name": "rndc_probability", "kind": "tab", "items": ["R", "C"], "random": "perCycle", "param": "probability", "x": 248, "row": 1, "labelW": 0, "width": 24, "fontsize": 9},
			{"name": "rnd_sustain", "label": "SUS", "kind": "num", "min": -100, "max": 100, "unit": "%", "random": "amount", "param": "sustain", "x": 273, "row": 1, "labelW": 30, "width": 34},
			{"name": "rndc_sustain", "kind": "tab", "items": ["R", "C"], "random": "perCycle", "param": "sustain", "x": 339, "row": 1, "labelW": 0, "width": 24, "fontsize": 9},
			{"name": "rnd_accent", "label": "ACC", "kind": "num", "min": -100, "max": 100, "unit": "%", "random": "amount", "param": "accent", "x": 364, "row": 1, "labelW": 30, "width": 34},
			{"name": "rndc_accent", "kind": "tab", "items": ["R", "C"], "random": "perCycle", "param": "accent", "x": 430, "row": 1, "labelW": 0, "width": 24, "fontsize": 9},
			{"name": "rnd_timing", "label": "TIM", "kind": "num", "min": -100, "max": 100, "unit": "%", "random": "amount", "param": "timing", "x": 0, "row": 2, "labelW": 30, "width": 34},
			{"name": "rndc_timing", "kind": "tab", "items": ["R", "C"], "random": "perCycle", "param": "timing", "x": 66, "row": 2, "labelW": 0, "width": 24, "fontsize": 9},
			{"name": "rnd_delay", "label": "DLY", "kind": "num", "min": -100, "max": 100, "unit": "%", "random": "amount", "param": "delay", "x": 91, "row": 2, "labelW": 30, "width": 34},
			{"name": "rndc_delay", "kind": "tab", "items": ["R", "C"], "random": "perCycle", "param": "delay", "x": 157, "row": 2, "labelW": 0, "width": 24, "fontsize": 9},
			{"name": "rnd_pitch", "label": "PITCH", "kind": "num", "min": -100, "max": 100, "unit": "%", "random": "amount", "param": "pitch", "x": 182, "row": 2, "labelW": 30, "width": 34},
			{"name": "rndc_pitch", "kind": "tab", "items": ["R", "C"], "random": "perCycle", "param": "pitch", "x": 248, "row": 2, "labelW": 0, "width": 24, "fontsize": 9},
			{"name": "rnd_voicing", "label": "VOIC", "kind": "num", "min": -100, "max": 100, "unit": "%", "random": "amount", "param": "voicing", "x": 273, "row": 2, "labelW": 30, "width": 34},
			{"name": "rndc_voicing", "kind": "tab", "items": ["R", "C"], "random": "perCycle", "param": "voicing", "x": 339, "row": 2, "labelW": 0, "width": 24, "fontsize": 9},
			{"name": "rnd_range", "label": "RANGE", "kind": "num", "min": -100, "max": 100, "unit": "%", "random": "amount", "param": "range", "x": 364, "row": 2, "labelW": 30, "width": 34},
			{"name": "rndc_range", "kind": "tab", "items": ["R", "C"], "random": "perCycle", "param": "range", "x": 430, "row": 2, "labelW": 0, "width": 24, "fontsize": 9},
			{"name": "rndVariance", "label": "VAR", "kind": "num", "min": 0, "max": 100, "unit": "%", "random": "variance", "x": 0, "row": 3, "labelW": 32, "width": 40},
			{"name": "rndRate", "label": "RATE", "kind": "menu", "items": "@randomRates", "random": "rate", "x": 91, "row": 3, "labelW": 32, "width": 56},
			{"name": "rndSlew", "label": "SLEW", "kind": "num", "min": 0, "max": 100, "unit": "%", "random": "slew", "x": 200, "row": 3, "labelW": 32, "width": 40},
			{"name": "rndLock", "label": "LOCK", "kind": "tab", "items": ["OFF", "ON"], "random": "locked", "x": 291, "row": 3, "labelW": 32, "width": 60},
			{"name": "rndSeq", "label": "SEQ", "kind": "buttons", "items": ["REGEN", "PHASE -", "PHASE +", "PROB -", "PROB +", "CLEAR ALL"], "randomAction": [0, 2, 3, 4, 5, 6], "row": 4, "labelW": 32}
		],
		"PATTERN": [
			{"name": "patternPlay", "label": "PLAY", "kind": "tab", "items": ["1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"], "action": "queue", "marks": "used"},
			{"name": "patternCopy", "label": "COPY TO", "alt": "TEMP TO", "altWhen": "@temp", "kind": "buttons", "items": ["1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"], "action": "copy", "marks": "used"},
			{"name": "patternClear", "label": "CLEAR", "kind": "buttons", "items": ["1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"], "action": "clear", "marks": "used"},
			{"name": "patternQuantize", "label": "SWITCH", "kind": "tab", "items": ["NOW", "BEAT", "BAR", "PATTERN"], "setting": "patternQuantize", "width": 200}
		],
		"SONG": [
			{"name": "songOn", "label": "SONG", "kind": "tab", "items": ["OFF", "ON"], "action": "songOn", "width": 60},
			{"name": "songLoop", "label": "LOOP", "kind": "tab", "items": ["OFF", "ON"], "action": "songLoop", "x": 128, "row": 0, "labelW": 30, "width": 60},
			{"name": "songResume", "kind": "button", "text": "BACK TO SONG", "action": "songResume", "x": 226, "row": 0, "labelW": 0, "width": 92,
				"showWhen": "songOverridden"},
			{"name": "songPage", "kind": "tab", "items": ["1-16", "17-32", "33-48", "49-64"], "action": "songPage", "x": 326, "row": 0, "labelW": 0, "width": 128,
				"marks": "songPages", "showWhen": "songPages"},
			{"name": "songSelect", "label": "ENTRIES", "kind": "tab", "items": ["1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"],
				"action": "songSelect", "marks": "songEntries", "row": 1},
			{"name": "songCount", "kind": "tab", "items": ["1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"],
				"action": "songSelect", "marks": "songCounts", "playBars": "above", "row": 2},
			{"name": "songSlot", "label": "PATTERN", "kind": "tab", "items": ["1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"],
				"action": "songSlot", "marks": "used", "activeWhen": "@songEntry", "row": 3},
			{"name": "songBars", "label": "BARS", "kind": "num", "min": 1, "max": 999, "action": "songBars", "activeWhen": "@songEntry", "width": 44, "row": 4},
			{"name": "songBarsSet", "kind": "buttons", "items": ["1", "2", "4", "8", "16", "32", "PAT"], "action": "songBarsSet", "activeWhen": "@songEntry",
				"x": 112, "row": 4, "labelW": 0, "width": 220},
			{"name": "songEdit", "label": "EDIT", "kind": "buttons", "items": ["ADD", "INSERT", "DELETE", "CLEAR SONG"], "action": "songEdit", "width": 260, "row": 5},
			{"name": "songInfo", "label": "STATUS", "kind": "text", "text": "songInfo", "row": 6}
		],
		"SETUP": [
			{"name": "cc", "label": "TYPE", "kind": "tab", "items": ["NOTE", "CC"], "field": "cc", "width": 100},
			{"name": "ccNumber", "label": "CC", "kind": "num", "min": 0, "max": 119, "field": "ccNumber", "width": 44,
				"activeWhen": {"field": "cc", "in": [1]}},
			{"name": "repeatTail", "label": "REPEATS", "kind": "tab", "items": ["CHOKE", "TAIL"], "field": "repeatTail", "width": 100},
			{"name": "seqMode", "label": "MODE", "alt": "CYC MODE", "altWhen": "@cycleEdit", "kind": "tab", "items": ["EUCLID", "SEQ"], "action": "seqMode", "width": 100},
			{"name": "channel", "label": "CHANNEL", "kind": "num", "min": 1, "max": 16, "field": "channel", "x": 180, "row": 0, "width": 44}
		],
		"DEVICE": [
			{"name": "tempTiming", "label": "TEMP OFF", "kind": "tab", "items": ["NOW", "BEAT", "BAR", "PATTERN"], "action": "tempTiming", "width": 200},
			{"name": "winZoom", "label": "WINDOW", "kind": "tab", "items": ["100%", "125%", "150%", "175%", "200%"], "action": "winZoom", "width": 220},
			{"name": "pageLock", "label": "PAGE LOCK", "kind": "tab", "items": ["OFF", "ON"], "action": "pageLock", "x": 290, "row": 0, "labelW": 60, "width": 100},
			{"name": "pageSync", "label": "PAGE SYNC", "kind": "tab", "items": ["OFF", "ON"], "action": "pageSync", "x": 290, "row": 1, "labelW": 60, "width": 100}
		],
		"MUTE": [
			{"name": "muteTiming", "label": "APPLY", "kind": "tab", "items": ["NOW", "BEAT", "BAR", "PATTERN"], "setting": "muteTiming", "width": 200},
			{"name": "muteCuts", "label": "ON MUTE", "kind": "tab", "items": ["FINISH", "CUT"], "setting": "muteCuts", "width": 120}
		]
	}
};
