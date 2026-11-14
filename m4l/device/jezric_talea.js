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
outlets = 6; // 0: MIDI now, "pitch velocity"   1: status text
             // receiver: 2: to [coll]: "store step entries...", "remove step", "clear"
             //           3: to [pipe]
             // brain: 2: "savelater" to a [delay] -> [deferlow] -> "savepattern" back
             //        3: the dials   4: to [pattr] (the pattern)
             //        5: the pads: "pad i r g b a", "page i", "follow 0/1", "mode i"

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
// Steps sent and not yet played, in order: { step, id (stepper's), perf (mutes and
// patterns as the engine stood after it), gates (bits: tracks starting a note in it) }.
var ahead = [];
var playing = null; // the step playing now, as in `ahead`, or null if unknown
var edits = {}; // "track key" -> value: dial edits waiting for the next tick
// Other edits waiting for the next tick, in order: { desc, fn (changes the engine), store
// (false: undo or redo, which Live already holds) }.
var ops = [];
// The step pads: the edit track's steps, 16 to a page.
var stepPage = 0;
var follow = true; // the page follows the playing step
var padKeys = []; // the colour each pad shows, to send only changes
// Pad modes, as on the Rack module: Step (the edit track's steps), Mute (a pad per track),
// Pattern (a pad per pattern slot).
var MODE_STEP = 0, MODE_MUTE = 1, MODE_PATTERN = 2, MODE_NAMES = ["Step", "Mute", "Pattern"];
var padMode = MODE_STEP;
var staged = 0; // Mute mode: tracks clicked, to change when Apply commits them (bits)
var mods = { shift: false, ctrl: false }; // modifier keys at the latest pad press
var blinks = 0; // [metro] ticks, for the staged and queued pads' blinking
var shownPattern = -1; // the pattern slot playing, as last shown (a switch: dials follow)
var newLoop = null; // a loop change waiting for the next tick
var editTrack = 0; // the track the brain's dials edit
// The track parameters the dials edit: all of them (Params.hpp), from the engine at init.
var EDIT_KEYS = ["steps", "pulses", "rotate", "division", "length", "repeats", "ramp", "repeatTime", "pace",
	"velocity", "probability", "sustain", "accent", "timing", "delay", "pitch", "voicing", "range"];
// On/off: off, the sequencer plays nothing. Switched on while Live plays, it starts at
// Live's next launch-quantization boundary, as a clip would.
var on = true;
var newOn = null; // a switch waiting for the next tick
var onIn = 0; // while playing: ticks until the steps play again (the quantization boundary)
var quantum = 16; // Live's launch quantization, in steps (16ths)
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
		observe("clip_trigger_quantization", function (v) { quantum = quantumSteps(Number(v[0])); });
		if (stepper) {
			EDIT_KEYS = stepper.keys.slice();
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
		readOn();
		showEdits();
		padKeys = [];
		outlet(5, "mode", padMode);
		outlet(5, "follow", follow ? 1 : 0);
		outlet(5, "page", stepPage);
		showPads();
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
	if (BRAIN) {
		brainTick(s, jump && !fromCue);
		showPads();
	} else receiverTick(s, jump, fromCue);
	if (stats.ticks % 16 === 0) status();
}
tick.immediate = 1;

// ---- brain -------------------------------------------------------------------------

// Sends step p: every track's entries, as "ev epoch p n0 entries0... n1 entries1...".
function sendStep(p) {
	if (!stepper) return;
	var silent = !on || (lastStep >= 0 && onIn > 0 && elapsed(lastStep, p) < onIn);
	var out = silent ? stepper.silence() : stepper.step(p), a = ["ev", epoch, p];
	for (var t = 0; t < TRACKS; t++) {
		var list = t < out.length ? out[t] : [];
		a.push(list.length);
		for (var i = 0; i < list.length; i++) a.push.apply(a, list[i]);
	}
	send.apply(null, a);
	sent[p] = a;
	delete sent[p - 64];
	var gates = 0;
	for (t = 0; t < out.length; t++)
		for (i = 0; i < out[t].length; i++) if (out[t][i][1] !== -1 && out[t][i][4] > 0) gates |= 1 << t;
	ahead.push({ step: p, id: silent ? -1 : stepper.id, perf: silent ? stepper.perf() : stepper.perfNow, gates: gates });
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
	if (onIn > 0) onIn--;
	if (jump) {
		restartAt(s);
		takeLoop();
		takeOn(s, true);
		takeEdits(-2);
	} else {
		takeLoop();
		takeOn(s, false);
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
	onIn = 0; // a start waiting for its boundary: Play starts it
	if (BRAIN) {
		sendCue();
		showPads();
	}
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

// ---- on/off (brain) ----------------------------------------------------------------

// Live's clip_trigger_quantization (0 None, 1 8 bars, 2 4 bars, 3 2 bars, 4 1 bar, 5 1/2,
// 6 1/2T, 7 1/4, 8 1/4T, 9 1/8, 10 1/8T, 11 1/16, 12 1/16T, 13 1/32) in 16th steps, 4/4.
// Triplets start on the straight boundary below them; None and 1/32 on the next step.
var QUANTUM_STEPS = [1, 128, 64, 32, 16, 8, 8, 4, 4, 2, 2, 1, 1, 1];

function quantumSteps(q) {
	return QUANTUM_STEPS[q] || 1;
}

// The device's patcher, to read the On button's value at init (a bang would toggle it).
var PATCHER = (typeof this !== "undefined" && this && this.patcher) ? this.patcher : null;

function readOn() {
	var o = PATCHER && PATCHER.getnamed ? PATCHER.getnamed("On") : null;
	if (o && typeof o.getvalueof === "function") enable(o.getvalueof());
}

// The On button: "enable 0/1". Taken on the next tick while playing.
function enable(v) {
	if (!BRAIN) return;
	var want = Number(v) !== 0;
	if (want === (newOn === null ? on : newOn)) return;
	newOn = want;
	if (lastStep < 0) takeOn(-1, false);
	status();
}

// Takes a waiting switch. Off: silent from the next step (what's sounding ends there).
// On while playing: silent until the next boundary of Live's launch quantization, then
// the engine starts there, as a clip launched now would.
function takeOn(s, jump) {
	if (newOn === null) return;
	on = newOn;
	newOn = null;
	if (s === -1) {
		onIn = 0;
		log("switched " + (on ? "on" : "off") + " while stopped");
		sendCue();
		return;
	}
	onIn = 0;
	if (on) {
		var q = s, k = 0;
		do { q = future(q, 1); k++; } while (q % quantum !== 0 && k < 1024);
		onIn = k;
	}
	log("switched " + (on ? "on at step " + s + ", playing from step " + future(s, onIn) : "off at step " + s));
	if (!jump) restartAt(future(s, 1));
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
	showPads();
}

// ---- the step pads (brain) -----------------------------------------------------------

// A pad: "pad i v" (v: 1 pressed). In Step mode it turns step (page * 16 + i) of the edit
// track off or on, taken on the next tick like a dial.
function pad(i, v) {
	// A press: "pad i 1", or "pad i bang" (a button-mode live.text bangs); a release, "pad i
	// 0", does nothing.
	if (!BRAIN || !stepper) return;
	if (v !== undefined && String(v) !== "bang" && Number(v) === 0) return;
	var shift = mods.shift, ctrl = mods.ctrl;
	mods = { shift: false, ctrl: false };
	i = Math.round(i);
	if (i < 0 || i >= 16) return;
	if (padMode === MODE_MUTE) return mutePad(i, ctrl);
	if (padMode === MODE_PATTERN) return patternPad(i, shift, ctrl);
	var t = editTrack, step = stepPage * 16 + i;
	log("pad " + (Math.round(i) + 1) + " pressed (" + v + "): track " + (t + 1) + " step " + (step + 1) + " of " + stepper.steps(t));
	if (step < 0 || step >= stepper.steps(t)) return;
	ops.push({ desc: "track " + (t + 1) + " step " + (step + 1) + " toggled", store: true,
		fn: function () { stepper.toggleStep(t, step); } });
	if (lastStep < 0) takeEdits(-1);
}

// The modifier keys, from [modifiers] just before each pad press: "padMods shift ctrl"
// (Ctrl on Windows, Cmd on the Mac).
function padMods(shift, ctrl) {
	mods = { shift: Number(shift) !== 0, ctrl: Number(ctrl) !== 0 };
}

// The time a change is made at, for the engine (quarter notes): while playing, the middle
// of the step playing (the boundary after it is the next step's start, if on one) or, for
// a change at once, the end of that step, so it's heard from the next; stopped, -1.
function changeTime(atOnce) {
	if (lastStep < 0) return -1;
	return atOnce ? (lastStep + 1) / 4 - 1e-9 : lastStep / 4 + 0.125;
}

// The pad mode menu: "padModeSelect i" (0 Step, 1 Mute, 2 Pattern). Leaving Mute mode drops
// the staged changes; choosing Mute again applies them.
function padModeSelect(i) {
	if (!BRAIN) return;
	i = Math.max(0, Math.min(2, Math.round(i)));
	if (i === MODE_MUTE && padMode === MODE_MUTE) return applyMutes();
	if (padMode === MODE_MUTE && staged) {
		log("left Mute mode: staged changes dropped");
		staged = 0;
	}
	padMode = i;
	log("pad mode " + MODE_NAMES[i]);
	showPads();
}

// Mute mode: a click stages a change to the track (Apply commits them); Ctrl+click mutes or
// unmutes it at once.
function mutePad(t, ctrl) {
	if (!ctrl) {
		staged ^= 1 << t;
		log("track " + (t + 1) + (staged & (1 << t) ? " staged" : " unstaged"));
		showPads();
		return;
	}
	staged &= ~(1 << t);
	ops.push({ desc: "track " + (t + 1) + " muted or unmuted at once", store: true, fn: function () {
		var muted = (stepper.perf().muted & (1 << t)) !== 0;
		stepper.setMuted(t, !muted, Math.max(0, changeTime(true)));
	} });
	if (lastStep < 0) takeEdits(-1);
}

// Apply (or Mute chosen again): the staged changes are committed, landing on the next mute
// boundary while playing, at once while stopped.
function applyMutes() {
	if (!BRAIN || !stepper || !staged) return;
	var toggle = staged;
	staged = 0;
	ops.push({ desc: "mute changes committed (tracks " + bitList(toggle) + ")", store: true, fn: function () {
		if (lastStep < 0) {
			var m = stepper.perf().muted;
			for (var t = 0; t < TRACKS; t++) if (toggle & (1 << t)) stepper.setMuted(t, !(m & (1 << t)), 0);
		} else stepper.commitMutes(toggle, changeTime(false));
	} });
	if (lastStep < 0) takeEdits(-1);
	else showPads();
}

function bitList(bits) {
	var out = [];
	for (var t = 0; t < TRACKS; t++) if (bits & (1 << t)) out.push(t + 1);
	return out.join(", ");
}

// Pattern mode: a click queues the slot for the next pattern boundary, Shift+click adds it
// to the chain, Ctrl+click switches at once. Stopped, everything switches at once, the
// pattern starting with the song.
function patternPad(slot, shift, ctrl) {
	ops.push({ desc: "pattern " + (slot + 1) + (shift ? " chained" : ctrl ? " at once" : " queued"), store: false,
		fn: function () {
			// Stopped: at time 0, so a chain's next pattern waits a whole pattern from there.
			var now = lastStep < 0 || ctrl;
			stepper.selectPattern(slot, shift, now, Math.max(0, changeTime(now)));
			if (lastStep < 0) stepper.cut = false; // nothing is sounding
		} });
	if (lastStep < 0) takeEdits(-1);
}

// The mutes and patterns the pads show: as playing now (the engine is ahead of it).
function perfShown() {
	if (lastStep >= 0 && playing && playing.perf) return playing.perf;
	return stepper.perf();
}

// A pattern switch heard now: the dials and step pads follow the new pattern, and it's
// stored with the set (as any edit).
function checkPattern(perf) {
	if (perf.current === shownPattern) return;
	var first = shownPattern < 0;
	shownPattern = perf.current;
	if (first) return;
	log("pattern " + (perf.current + 1) + " playing");
	showEdits();
	queueSave();
}

// The [metro] for blinking: the staged and queued pads.
function blink() {
	if (!BRAIN) return;
	blinks++;
	if (padMode !== MODE_STEP) showPads();
}

function pageCount() {
	return stepper ? Math.max(1, Math.ceil(stepper.steps(editTrack) / 16)) : 1;
}

// The step page tabs (0-3): pages past the track's Steps don't take. Choosing one turns
// Follow off, as on the Rack module.
function stepPageSelect(i) {
	if (!BRAIN) return;
	i = Math.round(i);
	if (i >= 0 && i < pageCount()) {
		stepPage = i;
		if (follow) { follow = false; outlet(5, "follow", 0); }
	}
	outlet(5, "page", stepPage);
	showPads();
}

function followSelect(v) {
	if (!BRAIN) return;
	follow = Number(v) !== 0;
	showPads();
}

// Rack's pad colours (Step mode).
var PAD_COLOURS = {
	none: [0.06, 0.06, 0.07],     // past the track's Steps
	cut: [0.02, 0.02, 0.02],      // cut off by Length
	cutHit: [0.24, 0.14, 0.04],
	off: [0.06, 0.06, 0.07],
	hit: [0.94, 0.54, 0.14],
	play: [0.35, 0.35, 0.35],     // the playing step
	playHit: [1, 1, 1],
};

// The edit track's step playing now, or -1 (stopped, off, or waiting to start).
function playingStep() {
	if (!stepper || lastStep < 0 || !on || onIn > 0) return -1;
	return stepper.stepAt(editTrack, lastStep / 4 + 1e-6);
}

// Sends the pads' colours, only those that changed. Follow moves the page with the
// playing step.
function showPads() {
	if (!BRAIN || !stepper) return;
	var perf = perfShown();
	checkPattern(perf);
	if (padMode === MODE_MUTE) return showMutePads(perf);
	if (padMode === MODE_PATTERN) return showPatternPads(perf);
	var t = editTrack, n = stepper.steps(t), played = stepper.playedSteps(t);
	var hits = stepper.hits(t), now = playingStep();
	var pages = pageCount();
	if (follow && now >= 0 && Math.floor(now / 16) !== stepPage) {
		stepPage = Math.floor(now / 16);
		outlet(5, "page", stepPage);
	}
	if (stepPage >= pages) {
		stepPage = pages - 1;
		outlet(5, "page", stepPage);
	}
	for (var i = 0; i < 16; i++) {
		var step = stepPage * 16 + i, hit = step < n && hits[step], key;
		if (step >= n) key = "none";
		else if (step >= played) key = hit ? "cutHit" : "cut";
		else if (step === now) key = hit ? "playHit" : "play";
		else key = hit ? "hit" : "off";
		setPad(i, PAD_COLOURS[key]);
	}
}

function setPad(i, c) {
	var key = c.join(" ");
	if (padKeys[i] === key) return;
	padKeys[i] = key;
	outlet(5, "pad", i, c[0], c[1], c[2], 1);
}

// Mute mode, as Rack's: orange plays (white on a note), blue-grey is muted. A staged change
// blinks slowly to the state it will become, a committed one quickly.
var MUTE_COLOURS = { on: [0.94, 0.54, 0.14], note: [1, 1, 1], muted: [0.12, 0.17, 0.28] };
function showMutePads(perf) {
	var fast = blinks % 2 === 1, slow = Math.floor(blinks / 4) % 2 === 1;
	var gates = lastStep >= 0 && playing ? playing.gates : 0;
	for (var t = 0; t < 16; t++) {
		var bit = 1 << t, muted = (perf.muted & bit) !== 0;
		if (perf.scheduled & bit) muted = muted !== fast;
		else if (staged & bit) muted = muted !== slow;
		setPad(t, muted ? MUTE_COLOURS.muted : gates & bit ? MUTE_COLOURS.note : MUTE_COLOURS.on);
	}
}

// Pattern mode, as Rack's: orange plays, blinking is queued, dim red is in the chain, dim
// orange holds a pattern, dark is the default pattern.
var PATTERN_COLOURS = { playing: [0.94, 0.54, 0.14], chain: [0.5, 0.06, 0.04], used: [0.24, 0.14, 0.04], empty: [0.06, 0.06, 0.07] };
function showPatternPads(perf) {
	var fast = blinks % 2 === 1;
	for (var i = 0; i < 16; i++) {
		var c;
		if (i === perf.current || (i === perf.queued && fast)) c = PATTERN_COLOURS.playing;
		else if (perf.chain.indexOf(i) >= 0) c = PATTERN_COLOURS.chain;
		else if (perf.used & (1 << i)) c = PATTERN_COLOURS.used;
		else c = PATTERN_COLOURS.empty;
		setPad(i, c);
	}
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
	if ((!keys.length && !ops.length) || !stepper) return;
	// Playing: back to the engine as it stood after the step playing now, before the
	// edits (its saved state holds the playing pattern, mutes and switches).
	var fresh = s >= 0 && rewindTo(0);
	var done = [], store = keys.length > 0;
	for (var i = 0; i < keys.length; i++) {
		var tk = keys[i].split(" "), v = edits[keys[i]];
		stepper.setParamByKey(Number(tk[0]), tk[1], v);
		done.push("track " + (Number(tk[0]) + 1) + " " + tk[1] + " " + v);
	}
	edits = {};
	var todo = ops;
	ops = [];
	todo.forEach(function (op) {
		op.fn();
		done.push(op.desc);
		if (op.store) store = true;
	});
	// The engine now stands after the step playing plus these edits: what the pads show.
	if (s >= 0 && playing) playing.perf = stepper.perf();
	showEdits();
	showPads();
	if (store) queueSave();
	if (s === -1) {
		log("edit while stopped: " + done.join(", "));
		sendCue();
	} else if (s === -2) {
		log("edit at step " + lastStep + " (after a jump): " + done.join(", "));
	} else {
		log("edit at step " + s + ": " + done.join(", "));
		if (!fresh) restartAt(future(lastStep, 1));
		sendAhead(lastStep);
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
	if (!rewindTo(k)) restartAt(future(lastStep, 1));
	sendAhead(lastStep);
}

// Puts the engine back as it stood after the k-th step sent ahead (0: the step playing),
// dropping the steps after it; false if that state isn't known.
function rewindTo(k) {
	var before = k === 0 ? playing : ahead[k - 1];
	if (!before || !stepper || !stepper.rewind(before.id)) return false;
	ahead.length = k;
	next = future(lastStep, k + 1);
	return true;
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
var PATTERN_FORMAT = 2; // 2: the bank (every pattern slot), as JSON; 1: one pattern
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
	if (Object.keys(edits).length || ops.length) return;
	var text = stepper.saveBank();
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
	if (args[0] !== PATTERN_FORMAT && args[0] !== 1) {
		log("[pattr] gave " + args.length + " values starting " + String(args[0]).slice(0, 20) + ": not a pattern, ignored");
		return;
	}
	var pieces = args.slice(1).map(function (x) { return String(x); });
	var text = pieces.every(function (x) { return x[0] === "p"; })
		? fromBase64(pieces.map(function (x) { return x.slice(1); }).join("")) : null;
	if (text === null) { warn("the pattern saved with the set isn't readable: keeping the current pattern"); return; }
	// Format 1 (sets saved before the bank): one pattern, which becomes slot 1's.
	if (args[0] === 1) text = JSON.stringify({ currentPattern: 0, patterns: [{ slot: 0, core: text }] });
	if (text === savedText) { log("[pattr] holds the stored pattern"); return; }
	if (!stepper) { pendingRestore = text; return; } // init takes it
	// After init this is Live's undo or redo: the whole pattern from Live, taken on the
	// next tick like an edit (so it's heard from the next step), and not stored back, so
	// redo still works. Edits not yet taken give way to it.
	if (!readable(text)) {
		warn("[pattr] gave a pattern that isn't readable: ignored");
		return;
	}
	savedText = text; // what [pattr] holds now: the pattern will equal it
	edits = {};
	ops = [{ desc: "undo or redo (the pattern from Live)", store: false, fn: function () {
		if (!stepper.loadBank(text)) warn("[pattr] gave a pattern that isn't readable: ignored");
		shownPattern = stepper.perf().current; // not a switch to store
	} }];
	log("undo or redo: the pattern from Live, taken " + (lastStep < 0 ? "now" : "on the next step"));
	if (lastStep < 0) takeEdits(-1);
}

// Whether the text is a bank the engine can load (tried, then the bank as it was goes back).
function readable(text) {
	try {
		var j = JSON.parse(text);
		if (!j || !Array.isArray(j.patterns)) return false;
	} catch (e) {
		return false;
	}
	var now = stepper.saveBank(), ok = stepper.loadBank(text);
	stepper.loadBank(now);
	return ok;
}

function applyPattern(text) {
	if (!stepper.loadBank(text)) {
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
		text = "Sequencer " + id + (stepper ? "" : " | NO ENGINE") + (on ? "" : " | OFF") +
			(onIn > 0 ? " | starts in " + onIn : "") + " | ahead " + lookaheadSteps +
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
		"editTrackSelect", "debug", "savepattern", "restore", "enable", "pad", "stepPageSelect", "followSelect", "padMods", "padModeSelect",
		"applyMutes", "blink"].forEach(function (n) {
		if (typeof g[n] !== "function") return;
		var f = guarded(n, g[n]);
		if (g[n].immediate) f.immediate = g[n].immediate; // keep scheduler-thread handlers there
		g[n] = f;
	});
})();
