// The sequencer's display: its pages and each page's rows, for the patch builder
// (device/build.py builds the controls from it) and the script (jezric_talea.js shows a
// page's controls, sets them from the engine and takes their edits). One row each:
//
//   name     the control's name: its scripting name is "d_<name>", its label's "dl_<name>"
//   label    the row's label, as on the Rack module's display ("alt": the label while the
//            row's "altWhen" setting is on, e.g. LOOPS while SYNC is LOOP)
//   kind     "tab" (a live.tab), "menu" (a live.menu), "buttons" (a live.tab in button
//            mode: each click an action, nothing stays selected), "num" (a live.numbox),
//            "text" (a line the script writes: "text" says which)
//   items    the choices; "@scales" is the engine's scale list (Tonal.hpp), "@randomRates"
//            its Random Rate names (Values.hpp)
//   values   the engine value of each choice, if not 0, 1, 2 ...
//   field    a track setting (jezric_talea_core.js FIELDS), for the edit track
//   setting  one of the sequencer's own settings (jezric_talea_core.js SETTINGS)
//   action   what a click does ("queue", "copy", "clear": pattern slots)
//   min, max a numbox's range
//   width    the control's width in px (default: the whole row)
//   activeWhen  dimmed unless that field's value is in the list
//   random   one of the edit track's random settings (jezric_talea_core.js RANDOM), of
//            parameter "param" for "amount" and "perCycle"; "randomAction": a buttons
//            row's RANDOM button numbers (talea_random_action)
//   x, row   where it goes: x px from the display's left edge, row number (default: the
//            next row, at the left); "labelW" the label's width ("label" left out: none)
//   unit     "%": a numbox shows its value in percent
//   fontsize a tab row's font size (default 8)
//   playBars a tab row with a bar under each choice, shown under the one playing
//            ("d_<name>_play<i>": CYCLE's playing cycle)
//   marks    "used": a pattern slot's choice reads "3*" while the slot holds a pattern;
//            "cycles": a cycle's "3*" with changes, "(3)" muted; "cycleClear": the choices
//            are what the selected cycle changes (ALL, HITS, the overridden parameters)
//
// "dialPages": the page a dial's turn shows (Rack's "display follows knobs").
// "diagrams": the pages with a diagram in the band under their rows
// (jezric_talea_diagram.js), and which; "boards": the pages drawn under their first row
// (the same script, a bigger box).
//
// "MUTE" isn't a page: Mute pad mode shows it over whatever page is chosen, as on Rack.
// No REPEAT page: its one setting, Choke or Tail, is on SETUP. VOICING is Rack's STYLE page
// (what the Voicing dial moves). Pages without rows aren't built yet; they show a note.
// SETUP also has the device's own controls (Instance, Look-ahead, the status line), placed
// by build.py.
//
// Strict JSON after "module.exports =", so build.py can read it.

module.exports = {
	"diagrams": {"GROOVE": "groove", "PHRASE": "phrase", "VOICING": "style", "SCALE": "scale"},
	"boards": {"TRACK": "steps", "KEYS": "keys"},
	"dialPages": {
		"steps": "TRACK", "pulses": "TRACK", "rotate": "TRACK", "division": "TRACK", "length": "TRACK",
		"repeats": "SETUP", "ramp": "SETUP", "repeatTime": "SETUP", "pace": "SETUP",
		"velocity": "GROOVE", "probability": "GROOVE", "sustain": "GROOVE", "accent": "GROOVE", "timing": "GROOVE", "delay": "GROOVE",
		"pitch": "SCALE", "voicing": "VOICING", "range": "PHRASE"
	},
	"pages": ["TRACK", "STEP", "CYCLE", "KEYS", "SCALE", "PHRASE", "VOICING", "GROOVE", "RANDOM", "PATTERN", "SONG", "SETUP"],
	"views": {
		"TRACK": [
			{"name": "trackInfo", "kind": "text", "text": "trackInfo", "labelW": 0}
		],
		"CYCLE": [
			{"name": "cycleCount", "label": "COUNT", "kind": "tab", "items": ["1", "2", "3", "4", "5", "6", "7", "8"], "action": "cycleCount"},
			{"name": "cycleSelect", "label": "CYCLE", "kind": "tab", "items": ["1", "2", "3", "4", "5", "6", "7", "8"], "action": "cycleSelect", "marks": "cycles", "playBars": true},
			{"name": "cycleEdit", "label": "EDIT", "kind": "tab", "items": ["OFF", "ON"], "action": "cycleEdit", "width": 60},
			{"name": "cycleMute", "label": "MUTE", "kind": "tab", "items": ["OFF", "ON"], "action": "cycleMute", "width": 60},
			{"name": "cycleClear", "label": "CLEAR", "kind": "buttons", "items": ["ALL", "HITS"], "action": "cycleClear", "marks": "cycleClear"},
			{"name": "cyclePlaying", "kind": "text", "text": "cyclePlaying", "labelW": 0}
		],
		"KEYS": [
			{"name": "keysOctave", "label": "OCTAVE", "kind": "buttons", "items": ["<", ">"], "action": "octave", "width": 50},
			{"name": "keysHelp", "kind": "text", "text": "keysHelp", "x": 120, "row": 0, "labelW": 0}
		],
		"SCALE": [
			{"name": "scale", "label": "SCALE", "kind": "menu", "items": "@scales", "field": "scale", "width": 180},
			{"name": "root", "label": "ROOT", "kind": "tab", "items": ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"], "field": "root"}
		],
		"PHRASE": [
			{"name": "phrase", "label": "PHRASE", "kind": "tab", "items": ["CAD 1", "CAD 2", "CAD 3", "CAD 4", "SAW", "TRI", "SINE", "PULSE", "USER"],
				"values": [0, 1, 2, 3, 4, 5, 6, 7, 9], "field": "phrase"},
			{"name": "phraseLength", "label": "BARS", "alt": "LOOPS", "altWhen": "phraseLoop", "kind": "tab",
				"items": ["1", "2", "4", "8", "16", "32", "64"], "field": "phraseLength", "width": 240},
			{"name": "phraseLoop", "label": "SYNC", "kind": "tab", "items": ["BARS", "LOOP"], "field": "phraseLoop", "width": 100}
		],
		"VOICING": [
			{"name": "style", "label": "STYLE", "kind": "tab", "items": ["FIXED", "RAMP", "CLIMB", "UP/DN", "CLB U/D"], "field": "style", "width": 260},
			{"name": "mono", "label": "PLAY", "kind": "tab", "items": ["POLY", "MONO"], "field": "mono", "width": 100},
			{"name": "voicingDown", "label": "VOICE", "kind": "tab", "items": ["UP", "DOWN"], "field": "voicingDown", "width": 100}
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
			{"name": "patternCopy", "label": "COPY TO", "kind": "buttons", "items": ["1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"], "action": "copy", "marks": "used"},
			{"name": "patternClear", "label": "CLEAR", "kind": "buttons", "items": ["1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"], "action": "clear", "marks": "used"},
			{"name": "patternQuantize", "label": "SWITCH", "kind": "tab", "items": ["NOW", "BEAT", "BAR", "PATTERN"], "setting": "patternQuantize", "width": 200}
		],
		"SETUP": [
			{"name": "cc", "label": "TYPE", "kind": "tab", "items": ["NOTE", "CC"], "field": "cc", "width": 100},
			{"name": "ccNumber", "label": "CC", "kind": "num", "min": 0, "max": 119, "field": "ccNumber", "width": 44,
				"activeWhen": {"field": "cc", "in": [1]}},
			{"name": "repeatTail", "label": "REPEATS", "kind": "tab", "items": ["CHOKE", "TAIL"], "field": "repeatTail", "width": 100}
		],
		"MUTE": [
			{"name": "muteTiming", "label": "APPLY", "kind": "tab", "items": ["NOW", "BEAT", "BAR", "PATTERN"], "setting": "muteTiming", "width": 200},
			{"name": "muteCuts", "label": "ON MUTE", "kind": "tab", "items": ["FINISH", "CUT"], "setting": "muteCuts", "width": 120}
		]
	}
};
