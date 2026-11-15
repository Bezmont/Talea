// Talea's engine, a step at a time, for the brain (jezric_talea.js) and the tests
// (tests/sim.js), so both turn the engine's events into MIDI the same way.
//
// step(p) runs the engine over step p's window and returns, per track, the MIDI entries
// for that step, each [offset, offPitch, 0, onPitch, onVelocity]:
//   offset    quarter notes after the step starts (0 <= offset < 0.25)
//   offPitch  a note to end first (0 = none: a note-off for note 0 is harmless)
//   onPitch, onVelocity   the note to start (velocity 0 = none)
// The receiver plays an entry as two MIDI notes "offPitch 0" and "onPitch onVelocity",
// in that order, so a note ending as the next starts can't cut the new one off. A chord
// is one entry per note, at the same offset.
//
// A CC track's values are entries [offset, -1, ccNumber, value, glide]: from `offset`,
// the receiver moves the controller from its last value to `value` over `glide`
// thousandths of a quarter note (0: at once; whole numbers, as [pipe] passes ints),
// sending each whole number on the way.
//
// Windows are (from, to], so step p's starts just before the step: a note exactly on
// the step is in its own step, with offset 0, never at the end of the step before.

var EPS = 1e-9;

// M: the engine module (jezric_talea_engine.js). preset, for tests only: a test pattern
// (tests/test_pattern.js) applied over the engine's default one.
function Stepper(M, preset) {
	var w = function (n, ret, args) { return M.cwrap("talea_" + n, ret, args); };
	this.M = M;
	this.process = w("process", "number", ["number", "number", "number"]);
	this.allOff = w("all_notes_off", "number", ["number", "number"]);
	this.time = w("event_time", "number", ["number", "number"]);
	this.track = w("event_track", "number", ["number", "number"]);
	this.on = w("event_on", "number", ["number", "number"]);
	this.note = w("event_note", "number", ["number", "number"]);
	this.velocity = w("event_velocity", "number", ["number", "number"]);
	this.chordSize = w("event_chord_size", "number", ["number", "number"]);
	this.chordNote = w("event_chord_note", "number", ["number", "number", "number"]);
	this.cc = w("event_cc", "number", ["number", "number"]);
	this.glide = w("event_glide", "number", ["number", "number"]);
	var key = w("param_key", "string", ["number"]), n = w("num_params", "number", [])();
	var keys = [];
	for (var i = 0; i < n; i++) keys.push(key(i));
	this.h = w("create", "number", [])();
	this.destroyFn = w("destroy", null, ["number"]);
	this.setParam = w("set_param", null, ["number", "number", "number", "number"]);
	this.getParamFn = w("get_param", "number", ["number", "number", "number"]);
	this.keys = keys;
	if (preset) preset.apply(this.setParam, this.h, function (k) { return keys.indexOf(k); });
	if (preset && preset.pattern) {
		var json = JSON.parse(w("save_pattern", "string", ["number"])(this.h));
		preset.pattern(json);
		if (!w("load_pattern", "number", ["number", "string"])(this.h, JSON.stringify(json)))
			throw new Error("jezric_talea_core: the engine didn't take the test pattern");
	}
	this.tracks = w("num_tracks", "number", [])();
	this.sounding = [];
	this.soundCh = []; // the channel (0-15) each track's sounding notes started on
	for (var t = 0; t < this.tracks; t++) {
		this.sounding.push([]);
		this.soundCh.push(t);
	}
	this.last = null; // the last step processed
	// After each step, the engine's state is saved, so it can carry on from any of the
	// last few steps again (rewind): numbered runs of steps, `count` so far.
	this.savePlayback = w("save_playback", null, ["number", "number"]);
	this.loadPlayback = w("load_playback", null, ["number", "number"]);
	this.slots = w("playback_slots", "number", [])();
	this.saved = [];
	this.count = 0;
}

// The MIDI entries of step p, per track, each with the channel (0-15) it goes to after its
// five numbers. A note ends on the channel it started on, whatever the track's channel is now
// (moved, or another pattern switched in). A step that doesn't follow the last one (a jump,
// a loop wrap, Play from somewhere else) first ends what was sounding, at offset 0.
Stepper.prototype.step = function (p) {
	var out = [], t, i, j, ch = [], sc = this.soundCh;
	for (t = 0; t < this.tracks; t++) {
		out.push([]);
		ch.push(this.channelOf(t));
	}
	var jump = this.last === null || p !== this.last + 1;
	if (jump || this.cut) {
		// What's sounding ends at the step's start: after a jump, or a change that cut the
		// engine's notes (a pattern switch at once).
		for (t = 0; t < this.tracks; t++) {
			for (i = 0; i < this.sounding[t].length; i++) out[t].push([0, this.sounding[t][i], 0, 0, 0, sc[t]]);
			this.sounding[t] = [];
		}
		this.cut = false;
	}
	if (jump) {
		this.allOff(this.h, p / 4 - EPS); // the engine forgets its notes; its events are dropped
		// A switched pattern carries on from the new time; a queued switch and mutes wait
		// for their boundaries counted from it.
		this.c("jump", null, ["number", "number"])(this.h, p / 4 - EPS);
	}
	this.last = p;
	var start = p / 4, n = this.process(this.h, start - EPS, start + 0.25 - EPS);
	for (i = 0; i < n; i++) {
		t = this.track(this.h, i);
		if (t < 0 || t >= this.tracks) continue;
		var o = Math.max(0, this.time(this.h, i) - start), s = this.sounding[t];
		var cc = this.cc(this.h, i);
		if (cc >= 0) {
			// A CC track's note-on is a value; its note-offs mean nothing.
			if (this.on(this.h, i)) out[t].push([o, -1, cc, this.velocity(this.h, i), Math.round(this.glide(this.h, i) * 1000), ch[t]]);
			continue;
		}
		if (this.on(this.h, i)) {
			// A note-on ends the track's previous notes first (as Rack's MIDI out does).
			for (j = 1; j < s.length; j++) out[t].push([o, s[j], 0, 0, 0, sc[t]]);
			// The first note-off goes with the first note-on, on one channel.
			var first = s.length ? s[0] : 0;
			if (first && sc[t] !== ch[t]) {
				out[t].push([o, first, 0, 0, 0, sc[t]]);
				first = 0;
			}
			var size = this.chordSize(this.h, i), vel = this.velocity(this.h, i), now = [];
			for (j = 0; j < size; j++) {
				var pitch = j === 0 ? this.note(this.h, i) : this.chordNote(this.h, i, j);
				out[t].push([o, j === 0 ? first : 0, 0, pitch, vel, ch[t]]);
				now.push(pitch);
			}
			this.sounding[t] = now;
			sc[t] = ch[t];
		} else {
			for (j = 0; j < s.length; j++) out[t].push([o, s[j], 0, 0, 0, sc[t]]);
			this.sounding[t] = [];
		}
	}
	this.perfNow = this.perf();
	this.id = this.count++;
	var k = this.id % this.slots;
	this.savePlayback(this.h, k);
	this.saved[k] = { id: this.id, step: p, sounding: this.sounding.map(function (x) { return x.slice(); }), soundCh: sc.slice() };
	return out;
};

// Puts the engine back as it was after the step numbered `id` (each step() gets the next
// number, in this.id), so the steps after it can be worked out again, e.g. with new
// settings. False if that state is no longer kept (only the last `slots` are).
Stepper.prototype.rewind = function (id) {
	var k = id % this.slots, s = this.saved[k];
	if (!s || s.id !== id) return false;
	this.loadPlayback(this.h, k);
	this.sounding = s.sounding.map(function (x) { return x.slice(); });
	this.soundCh = s.soundCh.slice();
	this.last = s.step;
	return true;
};

// A track's own value of a parameter, by key ("pulses", ...: Params.hpp).
Stepper.prototype.setParamByKey = function (track, key, v) {
	var i = this.keys.indexOf(key);
	if (i >= 0) this.setParam(this.h, track, i, v);
};

Stepper.prototype.getParam = function (track, key) {
	var i = this.keys.indexOf(key);
	return i >= 0 ? this.getParamFn(this.h, track, i) : 0;
};

// An engine function by name, wrapped once (the pads ask several times a step).
Stepper.prototype.c = function (name, ret, args) {
	var k = "_" + name;
	return this[k] || (this[k] = this.M.cwrap("talea_" + name, ret, args));
};

// The step pads: a pad click toggles a played step; the track's Steps, the steps that play
// before Length cuts the loop off, its hits (an array of booleans, by played step) and the
// step playing at time t (quarter notes since the song's start; -1 before it).
Stepper.prototype.toggleStep = function (track, step) {
	this.c("toggle_step", null, ["number", "number", "number"])(this.h, track, step);
};

Stepper.prototype.steps = function (track) {
	return this.c("steps", "number", ["number", "number"])(this.h, track);
};

Stepper.prototype.playedSteps = function (track) {
	return this.c("played_steps", "number", ["number", "number"])(this.h, track);
};

Stepper.prototype.hits = function (track) {
	var lo = this.c("hits_low", "number", ["number", "number"])(this.h, track);
	var hi = this.c("hits_high", "number", ["number", "number"])(this.h, track);
	var out = [];
	for (var i = 0; i < 64; i++) out.push(Math.floor((i < 32 ? lo : hi) / Math.pow(2, i % 32)) % 2 === 1);
	return out;
};

Stepper.prototype.stepAt = function (track, t) {
	return this.c("step_at", "number", ["number", "number", "number"])(this.h, track, t);
};

// ---- the display's settings ----
//
// Track settings that aren't dials, and the sequencer's own settings, by name (the
// numbers are talea_wasm.cpp's kFields and talea_get_setting's).
var FIELDS = ["style", "mono", "voicingDown", "repeatTail", "groove", "grooveLength", "grooveLoop",
	"swingSubdivision", "phrase", "phraseLength", "phraseLoop", "scale", "root", "cc", "ccNumber", "channel"];
var SETTINGS = ["muteTiming", "muteCuts", "patternQuantize"];
// A track's random settings (talea_get_random's numbers); amount and perCycle are per
// parameter (a key, as for the dials).
var RANDOM = ["amount", "perCycle", "variance", "rate", "phase", "probPhase", "slew", "locked"];

// c (here and in the field, diagram, pool and scale calls): the edited cycle in Cycle edit,
// whose own amounts / scale / pool / phrase they read and edit; left out or -1, the track.
function cyc(c) {
	return c === undefined || c === null ? -1 : c;
}

Stepper.prototype.getRandom = function (track, what, key, c) {
	return this.c("get_random", "number", ["number", "number", "number", "number", "number"])(this.h, track, cyc(c),
		RANDOM.indexOf(what), key === undefined || key === null ? -1 : this.keys.indexOf(key));
};

Stepper.prototype.setRandom = function (track, what, key, v, c) {
	this.c("set_random", null, ["number", "number", "number", "number", "number", "number"])(this.h, track, cyc(c),
		RANDOM.indexOf(what), key === undefined || key === null ? -1 : this.keys.indexOf(key), v);
};

// Whether cycle c has its own random amount of `key`.
Stepper.prototype.cycleRandomOwn = function (track, c, key) {
	return this.c("get_random", "number", ["number", "number", "number", "number", "number"])(this.h, track, c, 8,
		this.keys.indexOf(key)) === 1;
};

// The RANDOM page's buttons (talea_random_action): 0 Regen, 1 Lock or unlock (where the
// sequences are at time `now`), 2/3 phase -/+, 4/5 Probability phase -/+, 6 clear amounts.
Stepper.prototype.randomAction = function (track, action, now, c) {
	this.c("random_action", null, ["number", "number", "number", "number", "number"])(this.h, track, cyc(c), action, now);
};

// The MIDI channel (0-15) a track plays on: SETUP's CHANNEL (track n on channel n unless set).
Stepper.prototype.channelOf = function (track) {
	return this.getField(track, "channel") - 1;
};

// The fields a cycle can have its own of (phrase, scale, root): c applies to these only.
Stepper.prototype.getField = function (track, name, c) {
	return this.c("get_field", "number", ["number", "number", "number", "number"])(this.h, track, cyc(c), FIELDS.indexOf(name));
};

Stepper.prototype.setField = function (track, name, v, c) {
	this.c("set_field", null, ["number", "number", "number", "number", "number"])(this.h, track, cyc(c), FIELDS.indexOf(name), v);
};

Stepper.prototype.getSetting = function (name) {
	return this.c("get_setting", "number", ["number", "number"])(this.h, SETTINGS.indexOf(name));
};

Stepper.prototype.setSetting = function (name, v) {
	this.c("set_setting", null, ["number", "number", "number"])(this.h, SETTINGS.indexOf(name), v);
};

// The display's diagrams (talea_diagram): 0 groove shape, 1 phrase notes, 2 style notes,
// 3 the User phrase's levels; and the playhead across one (0..1, -1: none) at time t.
Stepper.prototype.diagram = function (track, kind, c) {
	var n = this.c("diagram", "number", ["number", "number", "number", "number"])(this.h, track, cyc(c), kind);
	var value = this.c("diagram_value", "number", ["number"]), out = [];
	for (var i = 0; i < n; i++) out.push(value(i));
	return out;
};

Stepper.prototype.diagramPhase = function (track, kind, t, c) {
	return this.c("diagram_phase", "number", ["number", "number", "number", "number", "number"])(this.h, track, cyc(c), kind, t);
};

Stepper.prototype.setPhraseLevel = function (track, col, level, c) {
	this.c("set_phrase_level", null, ["number", "number", "number", "number", "number"])(this.h, track, cyc(c), col, level);
};

// TRACK's info line; KEYS' pool (its notes) and the scale (12 bits by pitch class from C);
// a key click on KEYS (a pool note in or out) and on SCALE's notes (a pitch class).
Stepper.prototype.trackInfo = function (track) {
	return this.c("track_info", "string", ["number", "number"])(this.h, track);
};

Stepper.prototype.pool = function (track, c) {
	var n = this.c("pool_size", "number", ["number", "number", "number"])(this.h, track, cyc(c));
	var note = this.c("pool_note", "number", ["number", "number", "number", "number"]), out = [];
	for (var i = 0; i < n; i++) out.push(note(this.h, track, cyc(c), i));
	return out;
};

Stepper.prototype.scaleNotes = function (track, c) {
	return this.c("scale_notes", "number", ["number", "number", "number"])(this.h, track, cyc(c));
};

Stepper.prototype.poolKey = function (track, note, c) {
	this.c("pool_key", null, ["number", "number", "number", "number"])(this.h, track, cyc(c), note);
};

Stepper.prototype.scaleKey = function (track, pc, c) {
	this.c("scale_key", null, ["number", "number", "number", "number"])(this.h, track, cyc(c), pc);
};

// ---- SEQ mode ----
//
// c: the edited cycle in Cycle edit (its own mode and sequence), else -1.
Stepper.prototype.seqMode = function (track, c) {
	return this.c("seq_mode", "number", ["number", "number", "number"])(this.h, track, c) === 1;
};

// Switching to SEQ with an empty sequence fills it from the hits, at `velocity`.
Stepper.prototype.setSeqMode = function (track, c, on, velocity) {
	this.c("set_seq_mode", null, ["number", "number", "number", "number", "number"])(this.h, track, c, on ? 1 : 0, velocity);
};

// Step pos: { notes, velocity, gate }, or null for a rest.
Stepper.prototype.seqStep = function (track, c, pos) {
	var f = this.c("seq_step", "number", ["number", "number", "number", "number", "number"]), h = this.h;
	var n = f(h, track, c, pos, 0);
	if (!n) return null;
	var notes = [];
	for (var i = 0; i < n; i++) notes.push(f(h, track, c, pos, 3 + i));
	return { notes: notes, velocity: f(h, track, c, pos, 1), gate: f(h, track, c, pos, 2) };
};

// A step's own pool (EUCLID mode; KEYS with steps selected): its notes as cycle c plays it
// ([] = none, it plays the track's), a key click
// toggling a note in it, a copy from another step, and TRACK POOL (the lock goes).
Stepper.prototype.stepPool = function (track, c, pos) {
	var n = this.c("step_pool_size", "number", ["number", "number", "number", "number"])(this.h, track, c, pos);
	var note = this.c("step_pool_note", "number", ["number", "number", "number", "number", "number"]), out = [];
	for (var i = 0; i < n; i++) out.push(note(this.h, track, c, pos, i));
	return out;
};

Stepper.prototype.stepPoolKey = function (track, c, pos, note) {
	return !!this.c("step_pool_key", "number", ["number", "number", "number", "number", "number"])(this.h, track, c, pos, note);
};

Stepper.prototype.poolsUsed = function (track, c) {
	return !!this.c("pools_used", "number", ["number", "number", "number"])(this.h, track, c);
};

Stepper.prototype.stepPoolCopy = function (track, c, dst, src) {
	this.c("step_pool_copy", null, ["number", "number", "number", "number", "number"])(this.h, track, c, dst, src);
};

Stepper.prototype.stepPoolClear = function (track, c, pos) {
	this.c("step_pool_clear", null, ["number", "number", "number", "number"])(this.h, track, c, pos);
};

Stepper.prototype.seqToggleNote = function (track, c, pos, note, velocity) {
	this.c("seq_toggle_note", null, ["number", "number", "number", "number", "number", "number"])(this.h, track, c, pos, note, velocity);
};

Stepper.prototype.seqCopyChord = function (track, c, pos, from, velocity) {
	this.c("seq_copy_chord", null, ["number", "number", "number", "number", "number", "number"])(this.h, track, c, pos, from, velocity);
};

Stepper.prototype.seqClear = function (track, c, pos) {
	this.c("seq_clear", null, ["number", "number", "number", "number"])(this.h, track, c, pos);
};

// what 1 velocity, 2 gate (steps).
Stepper.prototype.seqSet = function (track, c, pos, what, v) {
	this.c("seq_set", null, ["number", "number", "number", "number", "number", "number"])(this.h, track, c, pos, what, v);
};

// ---- recording (REC, OVD) ----
//
// The notes played into the sequencer: snapped to the scale, then into the sequence, live
// (time in quarter notes) or step by step at the cursor.
Stepper.prototype.snapNote = function (track, c, note) {
	return this.c("snap_note", "number", ["number", "number", "number", "number"])(this.h, track, c, note);
};

Stepper.prototype.recOverdub = function (on) {
	this.c("rec_overdub", null, ["number", "number"])(this.h, on ? 1 : 0);
};

Stepper.prototype.recRelease = function () {
	this.c("rec_release", null, ["number"])(this.h);
};

Stepper.prototype.recSetCursor = function (pos) {
	this.c("rec_set_cursor", null, ["number", "number"])(this.h, pos);
};

Stepper.prototype.recCursor = function () {
	return this.c("rec_cursor", "number", ["number"])(this.h);
};

Stepper.prototype.recLiveOn = function (track, c, note, velocity, time) {
	this.c("rec_live_on", null, ["number", "number", "number", "number", "number", "number"])(this.h, track, c, note, velocity, time);
};

Stepper.prototype.recLiveOff = function (track, c, note, time) {
	this.c("rec_live_off", null, ["number", "number", "number", "number", "number"])(this.h, track, c, note, time);
};

Stepper.prototype.recStepOn = function (track, c, note, velocity) {
	this.c("rec_step_on", null, ["number", "number", "number", "number", "number"])(this.h, track, c, note, velocity);
};

Stepper.prototype.recStepOff = function (track, c, note) {
	this.c("rec_step_off", null, ["number", "number", "number", "number"])(this.h, track, c, note);
};

// ---- step locks ----
//
// c: the edited cycle in Cycle edit, else -1 (the track). pos: an unrotated step (where locks
// are kept): unrotated() turns a played step into one.
Stepper.prototype.unrotated = function (track, c, step) {
	return this.c("unrotated", "number", ["number", "number", "number", "number"])(this.h, track, c, step);
};

// The lock of `key` on step pos, or null.
Stepper.prototype.lockGet = function (track, c, pos, key) {
	var v = this.c("lock_get", "number", ["number", "number", "number", "number", "number"])(this.h, track, c, pos, this.keys.indexOf(key));
	return isNaN(v) ? null : v;
};

Stepper.prototype.lockSet = function (track, c, pos, key, v) {
	this.c("lock_set", null, ["number", "number", "number", "number", "number", "number"])(this.h, track, c, pos, this.keys.indexOf(key), v);
};

// key null: every lock of the step.
Stepper.prototype.lockClear = function (track, c, pos, key) {
	this.c("lock_clear", null, ["number", "number", "number", "number", "number"])(this.h, track, c, pos, key ? this.keys.indexOf(key) : -1);
};

// The parameters locked on any step, as keys.
Stepper.prototype.locksUsed = function (track, c) {
	var bits = this.c("locks_used", "number", ["number", "number", "number"])(this.h, track, c);
	return this.keys.filter(function (k, i) { return (bits >> i) & 1; });
};

Stepper.prototype.locksAt = function (track, c, pos) {
	return this.c("locks_at", "number", ["number", "number", "number", "number"])(this.h, track, c, pos) === 1;
};

Stepper.prototype.lockable = function (key) {
	return this.c("lockable", "number", ["number"])(this.keys.indexOf(key)) === 1;
};

// A parameter's range: [min, max].
Stepper.prototype.paramRange = function (key) {
	var i = this.keys.indexOf(key);
	return [this.M.cwrap("talea_param_min", "number", ["number"])(i), this.M.cwrap("talea_param_max", "number", ["number"])(i)];
};

Stepper.prototype.paramDefault = function (key) {
	return this.M.cwrap("talea_param_default", "number", ["number"])(this.keys.indexOf(key));
};

// ---- cycles ----
//
// A track's cycles (talea_cycle_*): c is a slot, 0..7.
Stepper.prototype.cycleCount = function (track) {
	return this.c("cycle_count", "number", ["number", "number"])(this.h, track);
};

Stepper.prototype.setCycleCount = function (track, n) {
	this.c("set_cycle_count", null, ["number", "number", "number"])(this.h, track, n);
};

// { muted, modified, ownHits, overrides (parameter keys) }
Stepper.prototype.cycleState = function (track, c) {
	var f = this.c("cycle_flags", "number", ["number", "number", "number"])(this.h, track, c);
	var ov = this.c("cycle_overrides", "number", ["number", "number", "number", "number"]);
	var bits = ov(this.h, track, c, 0), rbits = ov(this.h, track, c, 1);
	return { muted: (f & 1) !== 0, modified: (f & 2) !== 0, ownHits: (f & 4) !== 0, ownLocks: (f & 8) !== 0,
		ownTonal: (f & 16) !== 0, ownSeq: (f & 32) !== 0, ownMode: (f & 64) !== 0,
		overrides: this.keys.filter(function (k, i) { return (bits >> i) & 1; }),
		randomOverrides: this.keys.filter(function (k, i) { return (rbits >> i) & 1; }) };
};

Stepper.prototype.setCycleMuted = function (track, c, muted) {
	this.c("set_cycle_muted", null, ["number", "number", "number", "number"])(this.h, track, c, muted ? 1 : 0);
};

Stepper.prototype.getCycleParam = function (track, c, key) {
	return this.c("get_cycle_param", "number", ["number", "number", "number", "number"])(this.h, track, c, this.keys.indexOf(key));
};

Stepper.prototype.setCycleParam = function (track, c, key, v) {
	this.c("set_cycle_param", null, ["number", "number", "number", "number", "number"])(this.h, track, c, this.keys.indexOf(key), v);
};

// CLEAR: what 0 everything (not the mute), 1 the step edits, 2 the override of `key`.
Stepper.prototype.cycleClear = function (track, c, what, key) {
	this.c("cycle_clear", null, ["number", "number", "number", "number", "number"])(this.h, track, c, what,
		key ? this.keys.indexOf(key) : -1);
};

Stepper.prototype.toggleCycleStep = function (track, c, step) {
	this.c("toggle_cycle_step", null, ["number", "number", "number", "number"])(this.h, track, c, step);
};

Stepper.prototype.cycleHits = function (track, c) {
	var lo = this.c("cycle_hits_low", "number", ["number", "number", "number"])(this.h, track, c);
	var hi = this.c("cycle_hits_high", "number", ["number", "number", "number"])(this.h, track, c);
	var out = [];
	for (var i = 0; i < 64; i++) out.push(Math.floor((i < 32 ? lo : hi) / Math.pow(2, i % 32)) % 2 === 1);
	return out;
};

Stepper.prototype.cycleSteps = function (track, c) {
	return this.c("cycle_steps", "number", ["number", "number", "number"])(this.h, track, c);
};

Stepper.prototype.cyclePlayedSteps = function (track, c) {
	return this.c("cycle_played_steps", "number", ["number", "number", "number"])(this.h, track, c);
};

// Cycle edit: the edited cycle plays alone (-1: all in turn).
Stepper.prototype.setSoloCycle = function (track, c) {
	this.c("set_solo_cycle", null, ["number", "number", "number"])(this.h, track, c);
};

Stepper.prototype.cycleAt = function (track, t) {
	return this.c("cycle_at", "number", ["number", "number", "number"])(this.h, track, t);
};

Stepper.prototype.cycleInfo = function (track, c) {
	return this.c("cycle_info", "string", ["number", "number", "number"])(this.h, track, c);
};

// The first boundary of `timing` (0 now, 1 beat, 2 bar, 3 the pattern's loop) strictly
// after quarter note `now`.
Stepper.prototype.nextBoundary = function (timing, now) {
	return this.c("next_boundary", "number", ["number", "number", "number"])(this.h, timing, now);
};

// Temp: 1 on, 2 off reverting its changes, 3 off keeping them; 0 (or none) asks. Returns
// whether Temp is on.
Stepper.prototype.temp = function (action) {
	return this.c("temp", "number", ["number", "number"])(this.h, action || 0) !== 0;
};

// PATTERN page: the playing pattern copied into a slot (as it plays, Temp included; onto
// the playing slot it keeps Temp's changes), and a slot cleared.
Stepper.prototype.copyPattern = function (slot) {
	this.c("copy_pattern", null, ["number", "number"])(this.h, slot);
};

Stepper.prototype.clearPattern = function (slot) {
	this.c("clear_pattern", null, ["number", "number"])(this.h, slot);
};

// ---- mutes and pattern switches ----
//
// Times are quarter notes since the song's start: `now` is when the change is made.

// What the pads show in Mute and Pattern mode: { muted, scheduled (bits per track: muted,
// and committed changes waiting), current, queued (-1: none), used (bits per slot holding
// a pattern), chain (slots) }.
Stepper.prototype.perf = function () {
	var f = this.c("perf", "number", ["number", "number"]), h = this.h;
	var chain = [];
	for (var i = 0, n = f(h, 5); i < n; i++) chain.push(f(h, 6 + i));
	return { muted: f(h, 0), scheduled: f(h, 1), current: f(h, 2), queued: f(h, 3), used: f(h, 4), chain: chain };
};

// Commits mute changes (bits: tracks to toggle) for the next mute boundary after `now`.
Stepper.prototype.commitMutes = function (toggle, now) {
	this.c("commit_mutes", null, ["number", "number", "number"])(this.h, toggle, now);
};

// Mutes or unmutes a track at once.
Stepper.prototype.setMuted = function (track, muted, now) {
	this.c("set_muted", null, ["number", "number", "number", "number"])(this.h, track, muted ? 1 : 0, now);
};

// Queues a pattern slot, adds it to the chain, or switches at once (immediate). A switch at
// once ends what's sounding, at the next step's start.
Stepper.prototype.selectPattern = function (slot, chain, immediate, now) {
	this.c("select_pattern", null, ["number", "number", "number", "number", "number"])(this.h, slot, chain ? 1 : 0, immediate ? 1 : 0, now);
	if (immediate) this.cut = true; // (switching to the playing pattern restarts it)
};

// ---- the song ----
//
// On Live's timeline: its bar 1 is Live's bar 1. Edits take effect on the next bar line
// after `now` (quarter notes).

// { on, loop, count, bars (total), playing (entry, -1: none), overridden, pending, over,
// entries: [{ slot, bars }] }.
Stepper.prototype.song = function () {
	var f = this.c("song", "number", ["number", "number"]), h = this.h, n = f(h, 0), entries = [];
	for (var i = 0; i < n; i++) entries.push({ slot: f(h, 8 + 2 * i), bars: f(h, 9 + 2 * i) });
	return { count: n, bars: f(h, 1), loop: f(h, 2) === 1, on: f(h, 3) === 1, playing: f(h, 4),
		overridden: f(h, 5) === 1, pending: f(h, 6) === 1, over: f(h, 7) === 1, entries: entries,
		start: this.c("song_entry_start", "number", ["number"])(h) };
};

// Where the song is (after the latest step worked out): { playing, start, overridden,
// pending, over }.
Stepper.prototype.songNow = function () {
	var f = this.c("song", "number", ["number", "number"]), h = this.h;
	return { playing: f(h, 4), start: this.c("song_entry_start", "number", ["number"])(h),
		overridden: f(h, 5) === 1, pending: f(h, 6) === 1, over: f(h, 7) === 1 };
};

Stepper.prototype.songDefaultBars = function (slot) {
	return this.c("song_default_bars", "number", ["number", "number"])(this.h, slot);
};

Stepper.prototype.songSetOn = function (on, now) {
	this.c("song_set_on", null, ["number", "number", "number"])(this.h, on ? 1 : 0, now);
};

Stepper.prototype.songResume = function (now) {
	this.c("song_resume", null, ["number", "number"])(this.h, now);
};

Stepper.prototype.songSetLoop = function (on, now) {
	this.c("song_set_loop", null, ["number", "number", "number"])(this.h, on ? 1 : 0, now);
};

// bars < 1: the slot's own length.
Stepper.prototype.songInsert = function (at, slot, bars, now) {
	this.c("song_insert", null, ["number", "number", "number", "number", "number"])(this.h, at, slot, bars, now);
};

Stepper.prototype.songRemove = function (at, now) {
	this.c("song_remove", null, ["number", "number", "number"])(this.h, at, now);
};

// slot or bars -1: left as it is (a new slot brings its own length).
Stepper.prototype.songSet = function (i, slot, bars, now) {
	this.c("song_set", null, ["number", "number", "number", "number", "number"])(this.h, i, slot, bars, now);
};

Stepper.prototype.songClear = function () {
	this.c("song_clear", null, ["number"])(this.h);
};

// The whole bank (every pattern slot and which one plays) as JSON, and back: false if the
// text isn't a usable bank. Loading drops the queue, the chain and mutes waiting.
Stepper.prototype.saveBank = function () {
	return this.c("save_bank", "string", ["number"])(this.h);
};

Stepper.prototype.loadBank = function (text) {
	return this.c("load_bank", "number", ["number", "string"])(this.h, text) === 1;
};

// The pattern as JSON (Rack's "core" pattern format), and back: false if the text isn't
// a usable pattern. Loading changes the settings only; what's sounding carries on.
Stepper.prototype.savePattern = function () {
	return this.M.cwrap("talea_save_pattern", "string", ["number"])(this.h);
};

Stepper.prototype.loadPattern = function (text) {
	return this.M.cwrap("talea_load_pattern", "number", ["number", "string"])(this.h, text) === 1;
};

// A silent step (the sequencer switched off): ends what's sounding, at offset 0, and
// forgets the position, so the engine starts afresh at the next step().
Stepper.prototype.silence = function () {
	var out = [];
	for (var t = 0; t < this.tracks; t++) {
		var c = this.soundCh[t];
		out.push(this.sounding[t].map(function (n) { return [0, n, 0, 0, 0, c]; }));
		this.sounding[t] = [];
	}
	this.last = null;
	return out;
};

// COPY / PASTE / CLEAR (the clipboard of tracks, cycles, steps and patterns, in the engine's
// host): what "copy" / "paste" / "clear", on a "track" (copy, clear: t; paste: the copied
// track, the track it goes on), "cycle" (t, c), "step" (t, c, step: c the edited cycle or
// -1) or "pattern" (slot). No "clear" for a pattern slot: clearPattern.
Stepper.prototype.clip = function (what, kind) {
	var args = Array.prototype.slice.call(arguments, 2), types = ["number"];
	for (var i = 0; i < args.length; i++) types.push("number");
	this.c("clip_" + what + "_" + kind, null, types).apply(null, [this.h].concat(args));
};

// The copied tracks (a bit each) into pattern slot `slot`, on the same track numbers.
Stepper.prototype.clipPasteTracksToSlot = function (slot, mask) {
	this.c("clip_paste_tracks_to_slot", null, ["number", "number", "number"])(this.h, slot, mask);
};

// The track holds anything other than defaults.
Stepper.prototype.trackUsed = function (t) {
	return this.c("track_used", "number", ["number", "number"])(this.h, t) === 1;
};

// A parameter's value pasted on a cycle: its override there (past the last adds cycles).
Stepper.prototype.clipPasteCycleParam = function (track, c, key, v) {
	this.c("clip_paste_cycle_param", null, ["number", "number", "number", "number", "number"])(this.h, track, c, this.keys.indexOf(key), v);
};

// Forget the position, so the next step counts as a jump.
Stepper.prototype.forget = function () {
	this.last = null;
};

// Frees the engine (about 7 MB of the module's fixed 64 MB).
Stepper.prototype.destroy = function () {
	if (this.h) this.destroyFn(this.h);
	this.h = 0;
};

module.exports = { Stepper: Stepper, EPS: EPS, FIELDS: FIELDS, SETTINGS: SETTINGS, RANDOM: RANDOM };
