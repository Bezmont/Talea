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
	for (var t = 0; t < this.tracks; t++) this.sounding.push([]);
	this.last = null; // the last step processed
	// After each step, the engine's state is saved, so it can carry on from any of the
	// last few steps again (rewind): numbered runs of steps, `count` so far.
	this.savePlayback = w("save_playback", null, ["number", "number"]);
	this.loadPlayback = w("load_playback", null, ["number", "number"]);
	this.slots = w("playback_slots", "number", [])();
	this.saved = [];
	this.count = 0;
}

// The MIDI entries of step p, per track. A step that doesn't follow the last one (a jump,
// a loop wrap, Play from somewhere else) first ends what was sounding, at offset 0.
Stepper.prototype.step = function (p) {
	var out = [], t, i, j;
	for (t = 0; t < this.tracks; t++) out.push([]);
	if (this.last === null || p !== this.last + 1) {
		for (t = 0; t < this.tracks; t++) {
			for (i = 0; i < this.sounding[t].length; i++) out[t].push([0, this.sounding[t][i], 0, 0, 0]);
			this.sounding[t] = [];
		}
		this.allOff(this.h, p / 4 - EPS); // the engine forgets its notes; its events are dropped
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
			if (this.on(this.h, i)) out[t].push([o, -1, cc, this.velocity(this.h, i), Math.round(this.glide(this.h, i) * 1000)]);
			continue;
		}
		if (this.on(this.h, i)) {
			// A note-on ends the track's previous notes first (as Rack's MIDI out does).
			for (j = 1; j < s.length; j++) out[t].push([o, s[j], 0, 0, 0]);
			var size = this.chordSize(this.h, i), vel = this.velocity(this.h, i), now = [];
			for (j = 0; j < size; j++) {
				var pitch = j === 0 ? this.note(this.h, i) : this.chordNote(this.h, i, j);
				out[t].push([o, j === 0 && s.length ? s[0] : 0, 0, pitch, vel]);
				now.push(pitch);
			}
			this.sounding[t] = now;
		} else {
			for (j = 0; j < s.length; j++) out[t].push([o, s[j], 0, 0, 0]);
			this.sounding[t] = [];
		}
	}
	this.id = this.count++;
	var k = this.id % this.slots;
	this.savePlayback(this.h, k);
	this.saved[k] = { id: this.id, step: p, sounding: this.sounding.map(function (x) { return x.slice(); }) };
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

// The pattern as JSON (Rack's "core" pattern format), and back: false if the text isn't
// a usable pattern. Loading changes the settings only; what's sounding carries on.
Stepper.prototype.savePattern = function () {
	return this.M.cwrap("talea_save_pattern", "string", ["number"])(this.h);
};

Stepper.prototype.loadPattern = function (text) {
	return this.M.cwrap("talea_load_pattern", "number", ["number", "string"])(this.h, text) === 1;
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

module.exports = { Stepper: Stepper, EPS: EPS };
