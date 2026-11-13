// Talea for Max for Live: Talea Sequencer (the brain) and Talea Track Receiver.
//
//   [v8 jezric_talea.js brain]     runs Talea's engine (jezric_talea_engine.js: the C++
//                                  engine as WebAssembly), look-ahead steps early
//   [v8 jezric_talea.js receiver]  one per instrument track: plays one Talea track as MIDI
//
// The receiver's script never plays notes on time itself. For each step the brain sends
// every track's MIDI entries ([offset, offPitch, 0, onPitch, velocity], or CC values; see
// jezric_talea_core.js); the receiver writes its track's into a [coll] keyed by step. On
// each 16th the patch looks the step up and hands each entry to [pipe], delayed by its
// offset at Live's current tempo, all on Max's high-priority scheduler. So the script
// can be held up for up to look-ahead steps without a note being late.
//
// Stop, Play from elsewhere, a timeline click or a loop wrap restarts the engine's run:
// the brain counts these as epochs, so receivers can tell entries for the old position
// from the new.
//
// Messages to the Max Console: warnings and errors always; with "debug 1" (a toggle in
// the patch, not on the device's panel), a line for each edit, Play and stop as well.

autowatch = 1;
inlets = 1;
outlets = 5; // 0: MIDI now, "pitch velocity"   1: status text
             // receiver: 2: to [coll]: "store step entries...", "remove step", "clear"
             //           3: to [pipe]
             // brain: 2: "savelater" to a [delay] -> [deferlow] -> "savepattern" back
             //        3: the dials   4: to [pattr] (the pattern)

var ROLE = (typeof jsarguments !== "undefined" && jsarguments.length > 1) ? String(jsarguments[1]) : "receiver";
var BRAIN = ROLE === "brain";
var BUS = "jezric_talea_bus";
var IDS = ["A", "B", "C", "D"];
var TRACKS = 16;
var BEATS_PER_BAR = 4;
var UNITS_PER_STEP = 120;
var ENTRY = 5; // numbers per MIDI entry

var instance = 0;
var track = 0;
var lookaheadSteps = 4;
var loop = { on: false, start: 0, end: 0 };
var lastStep = -1;
var cueStep = -1;
var epoch = 0;
var connected = BRAIN;
var stats = { notes: 0, misses: 0, late: 0, received: 0, ticks: 0 };
var observers = [];
var lastTickTime = 0, gapAvg = 0;

// Brain
var stepper = null;
var next = -1; // the next step to send
var cueReady = false; // the cue's steps went out and Play hasn't moved since
var sent = {}; // step -> the "ev" message, to resend to a receiver that joins mid-play
var ahead = []; // steps sent and not yet played, in order: { step, id } (id: stepper's)
var playing = null; // the step playing now, as in `ahead`, or null if unknown
var edits = {}; // "track key" -> value: edits waiting for the next tick
var newLoop = null; // a loop change waiting for the next tick
var editTrack = 0; // the track the brain's dials edit
var EDIT_KEYS = ["pulses", "rotate", "repeats", "pace", "probability", "sustain", "timing", "delay"];
var startValues = null; // the dials' values in the pattern the engine started with, per track
// A test (tests/sim.js) gives the brain its test pattern as this global; Live doesn't.
var TEST_PATTERN = typeof TALEA_TEST_PATTERN !== "undefined" ? TALEA_TEST_PATTERN : null;
var debugOn = typeof TALEA_DEBUG !== "undefined" && !!TALEA_DEBUG;
var savedText = null; // the pattern as last stored in, or restored from, the set
var pendingRestore = null; // a pattern from the set, waiting for the engine (init)

// Receiver
var plan = {}; // step -> epoch of the entries written to [coll] for it
var got = {}; // step -> number of notes received for it (0 = an empty step)
var used = {}; // pitches this receiver has played, to end on a stop or jump
var epochAtTick = 0;

// ---- playback position (as in stage 2) -------------------------------------------

function future(s, k) {
	var p = s + k;
	if (loop.on && s < loop.end && p >= loop.end) p = loop.start + (p - loop.end);
	return p;
}

function elapsed(a, b) {
	if (b >= a) return b - a;
	if (loop.on && a <= loop.end && b >= loop.start) return (loop.end - a) + (b - loop.start);
	return 1e9;
}

// ---- bus and log -------------------------------------------------------------------

function send(selector) {
	var a = [BUS, selector, IDS[instance]];
	for (var i = 1; i < arguments.length; i++) a.push(arguments[i]);
	messnamed.apply(null, a);
}

function mine(id) {
	return String(id) === IDS[instance];
}

// A line in the Max Console: `log` only with debug on, `warn` always.
function log(text) {
	if (debugOn) warn(text);
}

function warn(text) {
	var who = BRAIN ? "Sequencer " + IDS[instance] : "Track Receiver " + IDS[instance] + " track " + (track + 1);
	var t = new Date();
	function pad(n, w) { n = "" + n; while (n.length < (w || 2)) n = "0" + n; return n; }
	var when = t.getFullYear() + "-" + pad(t.getMonth() + 1) + "-" + pad(t.getDate()) + " " +
		pad(t.getHours()) + ":" + pad(t.getMinutes()) + ":" + pad(t.getSeconds()) + "." +
		pad(t.getMilliseconds(), 3);
	post("Talea " + who + " at " + when + ": " + text + "\n");
}

function debug(v) {
	debugOn = Number(v) !== 0;
}

// ---- setup -------------------------------------------------------------------------

function observe(prop, fn) {
	var o = new LiveAPI(function (a) {
		if (a && a[0] === prop) guarded("observing " + prop, fn)(a.slice(1));
	}, "live_set");
	o.property = prop;
	observers.push(o);
}

// Live tells the script of loop changes at low priority; the brain takes them on its next
// tick (takeLoop), so the engine only ever runs from the tick.
function loopChanged() {
	var song = new LiveAPI("live_set");
	var on = Number(song.get("loop")) === 1;
	var start = Math.round(Number(song.get("loop_start")) * 4);
	var length = Math.round(Number(song.get("loop_length")) * 4);
	newLoop = { on: on, start: start, end: start + length };
	if (lastStep < 0) takeLoop(); // stopped: no tick to wait for
}

function cueChanged() {
	var t = Number(new LiveAPI("live_set").get("start_time"));
	cueStep = Math.max(0, Math.ceil(t * 4 - 1e-6));
	if (lastStep < 0) sendCue();
}

function init() {
	observers = [];
	observe("is_playing", function (v) {
		if (Number(v[0]) === 0) stopped();
	});
	if (BRAIN) {
		try {
			stepper = new (require("jezric_talea_core").Stepper)(require("jezric_talea_engine"), TEST_PATTERN);
		} catch (e) {
			warn("can't start the engine: " + e);
			stepper = null;
		}
		observe("loop", loopChanged);
		observe("loop_start", loopChanged);
		observe("loop_length", loopChanged);
		observe("start_time", cueChanged);
		if (stepper) {
			startValues = [];
			for (var t = 0; t < stepper.tracks; t++) startValues.push(EDIT_KEYS.map(function (k) { return stepper.getParam(t, k); }));
			// Live gives the device its saved values before it says the device is loaded,
			// so a pattern saved with the set is usually waiting here.
			if (pendingRestore !== null) {
				var text = pendingRestore;
				pendingRestore = null;
				applyPattern(text);
			}
		}
		loopChanged();
		cueChanged();
		showEdits();
		send("announce");
	} else {
		send("hello");
	}
	status();
}

function instanceSelect(i) {
	instance = Math.max(0, Math.min(IDS.length - 1, Math.round(i)));
	if (BRAIN) {
		send("announce");
		if (lastStep < 0) sendCue();
	} else {
		connected = false;
		forgetPlan();
		send("hello");
	}
	status();
}

function trackSelect(v) {
	track = Math.max(0, Math.min(TRACKS - 1, Math.round(v) - 1));
	if (!BRAIN) {
		forgetPlan();
		send("hello");
	}
	status();
}

function lookahead(v) {
	if (!BRAIN) return;
	var L = Math.max(4, Math.min(16, Math.round(v)));
	if (L === lookaheadSteps) return;
	lookaheadSteps = L;
	if (lastStep < 0) sendCue(); // while playing, the next tick sends further ahead
	status();
}

// ---- the tick ----------------------------------------------------------------------

// ---- timing: the script's time per step, logged at each stop (with debug on) ---------

var CLOCK = (typeof performance !== "undefined" && performance && typeof performance.now === "function")
	? "performance.now" : "Date.now";
function clockNow() { return CLOCK === "performance.now" ? performance.now() : Date.now(); }
var timing = null; // this Play's: { steps, sum, max, maxStep, over1, over5, margin }

function timingStart() {
	timing = { steps: 0, sum: 0, max: 0, maxStep: -1, over1: 0, over5: 0, margin: null };
}

function timingReport() {
	if (!timing || !timing.steps) return;
	var x = timing;
	log("timing: " + x.steps + " steps, script per step mean " + (x.sum / x.steps).toFixed(3) + " ms, max " +
		x.max.toFixed(3) + " ms (step " + x.maxStep + "), over 1 ms " + x.over1 + ", over 5 ms " + x.over5 +
		(BRAIN ? " (with sending to the receivers)" : ", closest step arrived " +
			(x.margin === null ? "-" : x.margin) + " steps ahead, misses " + stats.misses + ", late " + stats.late) +
		"; look-ahead " + lookaheadSteps + ", clock " + CLOCK);
	timing = null;
}

// One per 16th from [metro 16n @quantize 16n]. Scheduler thread: no LiveAPI here.
function tick(bars, beats, units) {
	var t0 = clockNow();
	tickBody(bars, beats, units);
	if (timing) {
		var dt = clockNow() - t0;
		timing.steps++;
		timing.sum += dt;
		if (dt > timing.max) { timing.max = dt; timing.maxStep = lastStep; }
		if (dt > 1) timing.over1++;
		if (dt > 5) timing.over5++;
	}
}

function tickBody(bars, beats, units) {
	var s = ((bars - 1) * BEATS_PER_BAR + (beats - 1)) * 4 + Math.round(units / UNITS_PER_STEP);
	var fromStop = lastStep < 0;
	if (fromStop) timingStart();
	var jump = fromStop || s !== future(lastStep, 1);
	if (jump && !fromStop) log("jump from step " + lastStep + " to " + s + " (transport " + bars + " " + beats + " " + units + ")");
	if (fromStop && cueStep >= 0 && s !== cueStep) log("Play started at step " + s + ", not the cue " + cueStep);
	if (fromStop && BRAIN) log("Play from step " + s + ", edited so far: " + editedSoFar());
	var fromCue = fromStop && s === cueStep;
	lastStep = s;
	stats.ticks++;
	if (BRAIN) brainTick(s, jump && !fromCue);
	else receiverTick(s, jump, fromCue);
	if (stats.ticks % 16 === 0) status();
}
tick.immediate = 1;

// ---- brain -------------------------------------------------------------------------

// Sends step p: every track's entries, as "ev epoch p n0 entries0... n1 entries1...".
function sendStep(p) {
	if (!stepper) return;
	var out = stepper.step(p), a = ["ev", epoch, p];
	for (var t = 0; t < TRACKS; t++) {
		var list = t < out.length ? out[t] : [];
		a.push(list.length);
		for (var i = 0; i < list.length; i++) a.push.apply(a, list[i]);
	}
	send.apply(null, a);
	sent[p] = a;
	delete sent[p - 64];
	ahead.push({ step: p, id: stepper.id });
}

// Starts a new run of the engine at step s: a new epoch.
function restartAt(s) {
	epoch++;
	sent = {};
	ahead = [];
	playing = null;
	if (stepper) stepper.forget();
	next = s;
}

// Sends the steps not yet sent, up to look-ahead steps past s.
function sendAhead(s) {
	var guard = 0;
	while (elapsed(s, next) <= lookaheadSteps && guard++ < 64) {
		sendStep(next);
		next = future(next, 1);
	}
}

// Logged at each Play: the values that differ from the pattern the engine started with.
function editedSoFar() {
	var out = [];
	if (!startValues || !stepper) return "?";
	for (var t = 0; t < startValues.length; t++)
		for (var i = 0; i < EDIT_KEYS.length; i++) {
			var v = stepper.getParam(t, EDIT_KEYS[i]);
			if (v !== startValues[t][i]) out.push("track " + (t + 1) + " " + EDIT_KEYS[i] + " " + v);
		}
	return out.length ? out.join(", ") : "none";
}

function brainTick(s, jump) {
	cueReady = false;
	// The step playing now leaves `ahead`.
	playing = null;
	while (ahead.length && !playing) {
		var a = ahead.shift();
		if (a.step === s) playing = a;
	}
	if (jump) {
		restartAt(s);
		takeLoop();
		takeEdits(-2);
	} else {
		takeLoop();
		takeEdits(s);
		// The next step to send should be at most look-ahead + 1 steps ahead. If not (a
		// change the brain heard of too late), start again from the next step.
		var d = elapsed(s, next);
		if (d < 1 || d > lookaheadSteps + 1) {
			warn("brain lost its place at step " + s + " (next to send " + next + "): resending");
			restartAt(future(s, 1));
		}
	}
	sendAhead(s);
}

// Stopped: the steps from the cue, so Play starts with them already in [coll].
function sendCue() {
	if (!BRAIN || cueStep < 0) return;
	restartAt(cueStep);
	sendAhead(cueStep);
	send("cue", epoch, cueStep);
	cueReady = true;
}

function stopped() {
	timingReport();
	lastStep = -1;
	if (BRAIN) sendCue();
	else {
		outlet(3, "clear");
		allOff();
		lastTickTime = 0;
	}
	status();
}

// A receiver joined or changed track: send it what it needs.
function hello(id) {
	if (!BRAIN || !mine(id)) return;
	send("loopinfo", loop.on ? 1 : 0, loop.start, loop.end);
	send("look", lookaheadSteps);
	if (lastStep < 0) sendCue();
	else for (var p in sent) if (elapsed(lastStep, Number(p)) >= 1 && elapsed(lastStep, Number(p)) < 64) send.apply(null, sent[p]);
}

// ---- edits (brain) -----------------------------------------------------------------

// A dial: "param key value", for the track the dials edit. Taken on the next tick.
function param(key, value) {
	if (!BRAIN || EDIT_KEYS.indexOf(String(key)) < 0 || !stepper) return;
	value = Number(value);
	if (stepper.getParam(editTrack, String(key)) === value && edits[editTrack + " " + key] === undefined) return;
	edits[editTrack + " " + key] = value;
	if (lastStep < 0) takeEdits(-1); // stopped: no tick to wait for
}

// The dials' track, from the menu (0 = track 1): they show its values.
function editTrackSelect(i) {
	if (!BRAIN) return;
	editTrack = Math.max(0, Math.min(TRACKS - 1, Math.round(i)));
	showEdits();
}

function showEdits() {
	if (!stepper) return;
	for (var i = 0; i < EDIT_KEYS.length; i++)
		outlet(3, EDIT_KEYS[i], stepper.getParam(editTrack, EDIT_KEYS[i]));
}

// Applies waiting edits. s: this tick's step while playing (the steps already sent are
// worked out again from the one playing now, as the engine stood after it, so an edit is
// heard from the next step on); -1 stopped; -2 on a jump (the restart covers it).
function takeEdits(s) {
	var keys = Object.keys(edits);
	if (!keys.length || !stepper) return;
	var done = [];
	for (var i = 0; i < keys.length; i++) {
		var tk = keys[i].split(" "), v = edits[keys[i]];
		stepper.setParamByKey(Number(tk[0]), tk[1], v);
		done.push("track " + (Number(tk[0]) + 1) + " " + tk[1] + " " + v);
	}
	edits = {};
	showEdits();
	queueSave();
	if (s === -1) {
		log("edit while stopped: " + done.join(", "));
		sendCue();
	} else if (s === -2) {
		log("edit at step " + lastStep + " (after a jump): " + done.join(", "));
	} else {
		log("edit at step " + s + ": " + done.join(", "));
		resendFrom(0);
	}
}

// Takes a waiting loop change. The steps sent ahead may now be the wrong ones (the loop
// was switched on or off, or moved, within look-ahead of its end): they are worked out
// again from the first that differs.
function takeLoop() {
	if (!newLoop) return;
	loop = newLoop;
	newLoop = null;
	send("loopinfo", loop.on ? 1 : 0, loop.start, loop.end);
	if (lastStep < 0) return;
	var k = 0;
	while (k < ahead.length && ahead[k].step === future(lastStep, k + 1)) k++;
	if (k < ahead.length) {
		log("loop changed during play (" + (loop.on ? "on, steps " + loop.start + "-" + (loop.end - 1) : "off") + "): resending from step " + future(lastStep, k + 1));
		resendFrom(k);
	}
}

// Keeps the first k steps sent ahead and works out the rest again, the engine carrying on
// as it stood after the step before them. Without that state (the step playing unknown,
// or too long ago), a fresh run from the next step.
function resendFrom(k) {
	var before = k === 0 ? playing : ahead[k - 1];
	if (before && stepper && stepper.rewind(before.id)) {
		ahead.length = k;
		next = future(lastStep, k + 1);
	} else restartAt(future(lastStep, 1));
	sendAhead(lastStep);
}

// ---- the pattern, saved with the Live set (brain) ------------------------------------
//
// A [pattr] set as a Stored Only Live parameter of type Blob holds the pattern, so Live saves
// it with the set and gives it back when the set loads. The pattern's JSON goes in as a
// list: the format number (1) and base64 pieces, each after a "p" (plain atoms: JSON's
// quotes, commas and braces would be read as Max syntax; a list must start with a number,
// or Max takes its first word as a message name, and the "p" keeps a piece from being
// read as a number). [pattr] sends back whatever it's given: "restore" ignores that.

var CHUNK = 2000;
var PATTERN_FORMAT = 1;
var B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

function toBase64(s) {
	var out = "", i, n, c = [];
	for (i = 0; i < s.length; i++) c.push(s.charCodeAt(i) & 0xff); // the JSON is ASCII
	for (i = 0; i < c.length; i += 3) {
		n = (c[i] << 16) | ((c[i + 1] || 0) << 8) | (c[i + 2] || 0);
		out += B64[(n >> 18) & 63] + B64[(n >> 12) & 63] +
			(i + 1 < c.length ? B64[(n >> 6) & 63] : "=") + (i + 2 < c.length ? B64[n & 63] : "=");
	}
	return out;
}

function fromBase64(s) {
	var out = "", i, n, a, b, c, d;
	for (i = 0; i + 3 < s.length; i += 4) {
		a = B64.indexOf(s[i]); b = B64.indexOf(s[i + 1]);
		c = s[i + 2] === "=" ? 0 : B64.indexOf(s[i + 2]); d = s[i + 3] === "=" ? 0 : B64.indexOf(s[i + 3]);
		if (a < 0 || b < 0 || c < 0 || d < 0) return null;
		n = (a << 18) | (b << 12) | (c << 6) | d;
		out += String.fromCharCode((n >> 16) & 255);
		if (s[i + 2] !== "=") out += String.fromCharCode((n >> 8) & 255);
		if (s[i + 3] !== "=") out += String.fromCharCode(n & 255);
	}
	return out;
}

// After an edit: store the pattern once the edits pause. "savelater" restarts a [delay]
// in the patch, which then sends "savepattern" back at low priority ([deferlow]), so a
// dial turned through many values is stored once: one step in Live's undo history.
function queueSave() {
	if (BRAIN) outlet(2, "savelater");
}

function savepattern() {
	if (!BRAIN || !stepper) return;
	// Edits waiting for a tick (an undo during play, say) store when they're taken.
	if (Object.keys(edits).length) return;
	var text = stepper.savePattern();
	if (text === savedText) return;
	savedText = text;
	var b = toBase64(text), a = [PATTERN_FORMAT];
	for (var i = 0; i < b.length; i += CHUNK) a.push("p" + b.slice(i, i + CHUNK));
	outlet(4, a);
	log("pattern stored for the set: " + text.length + " bytes, " + (a.length - 1) + " pieces");
}

// [pattr]'s value: from the set when it loads, or our own value sent back.
function restore() {
	if (!BRAIN) return;
	var args = Array.prototype.slice.call(arguments);
	if (args[0] !== PATTERN_FORMAT) {
		log("[pattr] gave " + args.length + " values starting " + String(args[0]).slice(0, 20) + ": not a pattern, ignored");
		return;
	}
	var pieces = args.slice(1).map(function (x) { return String(x); });
	var text = pieces.every(function (x) { return x[0] === "p"; })
		? fromBase64(pieces.map(function (x) { return x.slice(1); }).join("")) : null;
	if (text === null) { warn("the pattern saved with the set isn't readable: keeping the current pattern"); return; }
	if (text === savedText) { log("[pattr] holds the stored pattern"); return; }
	if (!stepper) { pendingRestore = text; return; } // init takes it
	// After init this is Live's undo or redo: the values that differ become edits, taken
	// and logged like a dial's.
	var now = stepper.savePattern();
	if (!stepper.loadPattern(text)) {
		warn("[pattr] gave a pattern that isn't readable: ignored");
		return;
	}
	var want = [];
	for (var t = 0; t < startValues.length; t++)
		for (var i = 0; i < EDIT_KEYS.length; i++) want.push([t, EDIT_KEYS[i], stepper.getParam(t, EDIT_KEYS[i])]);
	stepper.loadPattern(now);
	savedText = text; // what [pattr] holds now: the edits leave the pattern equal to it
	var n = 0;
	want.forEach(function (x) {
		if (stepper.getParam(x[0], x[1]) !== x[2] || edits[x[0] + " " + x[1]] !== undefined) {
			edits[x[0] + " " + x[1]] = x[2];
			n++;
		}
	});
	log("undo or redo: the pattern from Live changes " + n + " value" + (n === 1 ? "" : "s"));
	if (n && lastStep < 0) takeEdits(-1);
}

function applyPattern(text) {
	if (!stepper.loadPattern(text)) {
		warn("the pattern saved with the set isn't readable: keeping the current pattern");
		return false;
	}
	savedText = text;
	log("pattern restored from the set, edited: " + editedSoFar());
	showEdits();
	return true;
}

// ---- receiver ----------------------------------------------------------------------

function allOff() {
	for (var p in used) outlet(0, Number(p), 0);
}

function forgetPlan() {
	outlet(2, "clear");
	plan = {};
	got = {};
}

// Drops [coll] entries from before epoch e.
function dropBefore(e) {
	for (var p in plan) {
		if (plan[p] < e) {
			outlet(2, "remove", Number(p));
			delete plan[p];
			delete got[p];
		}
	}
}

function ev(id, ep, p) {
	if (BRAIN || !mine(id)) return;
	if (ep < epoch) return; // for a position playback has left
	if (ep > epoch) {
		epoch = ep;
		dropBefore(ep);
	}
	// Find this track's entries.
	var i = 3, entries = [];
	for (var t = 0; t < TRACKS && i < arguments.length; t++) {
		var n = arguments[i++];
		if (t === track) entries = Array.prototype.slice.call(arguments, i, i + n * ENTRY);
		i += n * ENTRY;
	}
	connected = true;
	stats.received++;
	if (p === lastStep) {
		// Too late to play. Right after a jump that's expected for the step jumped to
		// (the brain only hears of the jump on that step), so only count it otherwise.
		if (ep === epochAtTick) {
			stats.late++;
			warn("LATE: step " + p + " came after it had played");
		}
		return;
	}
	// How many steps ahead of playing it this step first came (look-ahead's margin; a
	// step sent again after an edit comes later by design).
	if (timing && lastStep >= 0 && ep === epochAtTick && got[p] === undefined) {
		var ahead = elapsed(lastStep, p);
		if (ahead > 0 && ahead < 64 && (timing.margin === null || ahead < timing.margin)) timing.margin = ahead;
	}
	var notes = 0; // notes and CC values
	for (var k = 0; k < entries.length; k += ENTRY) {
		if (entries[k + 1] === -1) { notes++; continue; } // a CC value
		if (entries[k + 4] > 0) { notes++; used[entries[k + 3]] = true; }
		if (entries[k + 1] > 0) used[entries[k + 1]] = true;
	}
	got[p] = notes;
	if (entries.length) {
		outlet(2, ["store", p].concat(entries));
		plan[p] = ep;
	} else if (plan[p] !== undefined) {
		outlet(2, "remove", p);
		delete plan[p];
	}
}
ev.immediate = 1;

function receiverTick(s, jump, fromCue) {
	var now = Date.now();
	if (lastTickTime > 0 && !jump) {
		var gap = now - lastTickTime;
		if (gapAvg > 0 && gap > 1.5 * gapAvg + 20) log("script ran " + Math.round(gap - gapAvg) + " ms late at step " + s);
		gapAvg = gapAvg > 0 ? 0.9 * gapAvg + 0.1 * Math.min(gap, 2 * gapAvg) : gap;
	}
	lastTickTime = now;
	if (!connected) return;

	// [coll] has just played step s natively (and deleted it), if it was there.
	var had = got[s];
	delete plan[s];
	delete got[s];
	if (jump && !fromCue) {
		// What [pipe] still holds and what [coll] holds were for the old position, unless
		// the brain has already sent this tick's new epoch.
		outlet(3, "clear");
		allOff();
		if (epoch > epochAtTick) dropBefore(epoch);
		else forgetPlan();
	} else if (had === undefined) {
		stats.misses++;
		warn("MISS: step " + s + " wasn't ready");
	} else stats.notes += had;
	epochAtTick = epoch;
}

function cue(id, ep, step) {
	if (BRAIN || !mine(id)) return;
	cueStep = step;
	if (ep > epoch) {
		epoch = ep;
		dropBefore(ep);
	}
	epochAtTick = epoch;
}

function loopinfo(id, on, start, end) {
	if (!BRAIN && mine(id)) loop = { on: on === 1, start: start, end: end };
}

function look(id, L) {
	if (!BRAIN && mine(id)) lookaheadSteps = L;
}

function announce(id) {
	if (!BRAIN && mine(id)) send("hello");
}

function anything() {
}

// ---- status ------------------------------------------------------------------------

function status() {
	var id = IDS[instance], text;
	if (BRAIN) {
		text = "Sequencer " + id + (stepper ? "" : " | NO ENGINE") + " | ahead " + lookaheadSteps +
			" | step " + lastStep + " | epoch " + epoch + (loop.on ? " | loop" : "");
	} else if (!connected) {
		text = "Waiting for sequencer " + id;
	} else {
		text = id + " trk " + (track + 1) + " | notes " + stats.notes + " | MISSES " + stats.misses +
			" | late " + stats.late + " | rx " + stats.received;
	}
	outlet(1, "set", text);
}

function reset() {
	stats = { notes: 0, misses: 0, late: 0, received: 0, ticks: 0 };
	status();
}

// ---- errors ------------------------------------------------------------------------

// An error in a message handler: to the Max Console with where it happened, and the
// handler carries on with the next message.
function guarded(name, fn) {
	return function () {
		try {
			return fn.apply(this, arguments);
		} catch (e) {
			warn("ERROR in " + name + ": " + e + (e && e.stack ? " | " + String(e.stack).split("\n").slice(1, 4).join(" <- ") : ""));
		}
	};
}
(function () {
	var g = this;
	["init", "instanceSelect", "trackSelect", "lookahead", "tick", "hello", "ev", "cue",
		"loopinfo", "look", "announce", "anything", "status", "reset", "param",
		"editTrackSelect", "debug", "savepattern", "restore"].forEach(function (n) {
		if (typeof g[n] !== "function") return;
		var f = guarded(n, g[n]);
		if (g[n].immediate) f.immediate = g[n].immediate; // keep scheduler-thread handlers there
		g[n] = f;
	});
})();
