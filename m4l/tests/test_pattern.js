// The test pattern for tests/sim.js: one kind of off-grid timing per track on tracks
// 1-4, chords on track 5 and a CC track on track 6. The simulation gives it to the brain
// (as the global TALEA_TEST_PATTERN) and checks what plays against the engine run on it
// directly. It was also the pattern of the recorded tests in Live (the proof of
// concept's stage 3).
//
// Tracks 1-4: mono notes, velocities kept high (no Ramp, no velocity random), so every
// note is a clear burst in audio.

var SETTINGS = [
	// track, parameter key, value
	// Track 1: swing. 9 of 16, every 2nd 16th late.
	[0, "pulses", 9], [0, "timing", 50], [0, "sustain", 40],
	// Track 2: 16th triplets. 7 of 12 at 1/16t.
	[1, "steps", 12], [1, "pulses", 7], [1, "division", 8], [1, "sustain", 60],
	// Track 3: repeats. 4 of 16, 3 repeats each, accelerating (Pace).
	[2, "pulses", 4], [2, "repeats", 3], [2, "pace", 40], [2, "sustain", 30],
	// Track 4: Delay (every note late) and Probability (notes skipped, keyed to position).
	[3, "steps", 16], [3, "pulses", 6], [3, "delay", 30], [3, "probability", 25],
	// Track 5: chords. 5 of 16, a four-note chord whose voicing climbs (Style Ramp), so
	// consecutive chords share some notes and not others. 9 of 16 at Sustain 100: a chord
	// lasts an 8th, so one an 8th later starts as it ends, and one a 16th later ends it
	// (a shared note ends and starts again at once).
	[4, "pulses", 9], [4, "voicing", 3], [4, "sustain", 100],
	// Track 6: CC 74. 5 of 16 (gaps of 3 and 4 16ths), values from Accent, gliding over
	// 60 % of each gap; swing puts every 2nd 16th late.
	[5, "pulses", 5], [5, "accent", 80], [5, "sustain", 60], [5, "timing", 50]
];

// Track settings that aren't parameters, as in a saved pattern's JSON.
var TRACK_SETTINGS = {
	4: { pool: [60, 64, 67, 71], style: 1, mono: false },
	5: { cc: true, ccNumber: 74 }
};

var TRACKS = 6;

// Applies the pattern through the engine's set_param(handle, track, param, value); `index`
// maps a parameter key to its number. `pattern`, if given, edits the saved pattern's JSON
// (an object) for the rest.
function apply(setParam, h, index) {
	for (var i = 0; i < SETTINGS.length; i++) {
		var s = SETTINGS[i], p = index(s[1]);
		if (p < 0) throw new Error("test_pattern: no parameter " + s[1]);
		setParam(h, s[0], p, s[2]);
	}
}

function pattern(json) {
	for (var t in TRACK_SETTINGS)
		for (var k in TRACK_SETTINGS[t]) {
			if (!(k in json.tracks[t])) throw new Error("test_pattern: no track setting " + k);
			json.tracks[t][k] = TRACK_SETTINGS[t][k];
		}
}

module.exports = { apply: apply, pattern: pattern, TRACKS: TRACKS, SETTINGS: SETTINGS };
