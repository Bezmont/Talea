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
outlets = 7; // 0: MIDI now, "pitch velocity"   1: status text
             // receiver: 2: to [coll]: "store step entries...", "remove step", "clear"
             //           3: to [pipe]
             // brain: 2: "savelater" to a [delay] -> [deferlow] -> "savepattern" back
             //        3: the dials   4: to [pattr] (the pattern)
             //        5: the pads: "pad i r g b a", "page i", "follow 0/1", "mode i",
             //           "apply 0/1", "rec 0/1", "ovd 0/1"
             //        6: the display: "script show|hide name", "d_<row> value",
             //           "dl_<row> label", "dr_<row> choices...", "da_<row> 0/1" (active)

var ROLE = (typeof jsarguments !== "undefined" && jsarguments.length > 1) ? String(jsarguments[1]) : "receiver";
var BRAIN = ROLE === "brain";
var BUS = "jezric_talea_bus";
var IDS = ["A", "B", "C", "D"];
var TRACKS = 16;
var BEATS_PER_BAR = 4;
var UNITS_PER_STEP = 120;
var ENTRY = 5; // numbers per MIDI entry

var instance = 0;
var channel = 0; // receiver: the MIDI channel it plays (0-15): every track sent on it
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
// Cycles (the CYCLE page): the one selected, and Cycle edit (the dials and step pads edit
// it, and it plays alone).
var selCycle = 0;
var cycleEdit = false;
// Step locks: the steps Ctrl+clicked on the pads (unrotated positions -> true). While any
// are, the dials that can lock show the first one's lock and set it on all of them.
var lockSel = {};
// SEQ mode: the step a plain click on a pad selected (an unrotated position, -1 none); KEYS
// edits its chord (or the lock selection's).
var seqSel = -1;
// SEQ recording: REC and OVD, the song time of the latest note played (quarter notes), and
// the notes held: played pitch -> the note it sounds as (snapped), and how many keys hold
// each sounding note (two keys snapping to one note play it once).
var rec = false, overdub = false, noteAt = 0, held = {}, holding = {}, recorded = false;
var laneKeys = []; // the LOCKS page's lanes, top to bottom
var lanePage = 0; // which 6 lanes LOCKS shows, when more parameters are locked
var laneFocus = null; // a parameter just locked by a dial: its lanes are shown
var padMode = MODE_STEP;
var staged = 0; // Mute mode: tracks clicked, to change when Apply commits them (bits)
var mods = { shift: false, ctrl: false, alt: false }; // modifier keys at the latest pad press
var blinks = 0; // [metro] ticks, for the staged and queued pads' blinking
var shownPattern = -1; // the pattern slot playing, as last shown (a switch: dials follow)
var preMuteMode = MODE_STEP; // the pad mode before Mute (a display page tab goes back to it)
// The display (jezric_talea_display.js): the page chosen, the view shown (a page, or MUTE
// in Mute mode) and what each row's control shows, to send only changes.
var DISPLAY = null;
var dispPage = 0;
var shownView = null;
var dispShown = {};
var relabelled = {}; // row -> when its choices were renamed (ms), to ignore an echo
var keysOctave = 2; // KEYS: the keyboard's lowest octave (0: from C0; 4 octaves shown)
// SONG: the entry selected (= the entry count: none, the "+" cell) and the page of 16 shown.
var songSel = 0, songPage = 0;
var SONG_PAGE = 16, SONG_MAX = 64;
// COPY / PASTE / CLEAR: the one armed ("copy", "paste", "clear" or null), CLEAR staying armed
// (Ctrl+click), the clipboard ({ kind: "track" / "step" / "pattern" / "cycle" / "param",
// source, key, value }; the data of the first four is in the engine's host) and when a target
// last took a click (a dial doesn't turn just after).
var armed = null, clearSticky = false, clip = null, clipClickAt = null;
var clipCtrl = false; // Ctrl held on the latest target click (PASTE stays armed)
var newLoop = null; // a loop change waiting for the next tick
var editTrack = 0; // the track the brain's dials edit
var group = 0; // the other tracks grouped with it (bits): plain dial turns and page settings edit them too
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
	var who = BRAIN ? "Sequencer " + IDS[instance] : "Track Receiver " + IDS[instance] + " channel " + (channel + 1);
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
			DISPLAY = require("jezric_talea_display");
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
				if (applyPattern(text)) restoreUi(text);
			}
		}
		loopChanged();
		cueChanged();
		readOn();
		showEdits();
		padKeys = [];
		outlet(5, "mode", padMode);
		outlet(5, "apply", 0);
		outlet(6, "d_pages", dispPage);
		shownView = null;
		showView();
		outlet(5, "follow", follow ? 1 : 0);
		outlet(5, "page", stepPage);
		tempShown = null;
		tempPending = null;
		tempReadyAt = Date.now() + 2000;
		gearShown = null;
		showGear();
		showTemp();
		clipShown = null;
		showClip();
		trackKeys = [];
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

// Receiver: its Channel (1-16).
function channelSelect(v) {
	channel = Math.max(0, Math.min(15, Math.round(v) - 1));
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
	if (fromStop) {
		timingStart();
		if (BRAIN && stepper) stepper.recRelease();
	}
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

// Sends step p: every channel's entries (of all the tracks on it), as "ev epoch p n0
// entries0... n1 entries1..." for channels 1-16.
function sendStep(p) {
	if (!stepper) return;
	var silent = !on || (lastStep >= 0 && onIn > 0 && elapsed(lastStep, p) < onIn);
	var out = silent ? stepper.silence() : stepper.step(p), a = ["ev", epoch, p], byCh = [], t, i;
	for (i = 0; i < 16; i++) byCh.push([]);
	for (t = 0; t < out.length; t++)
		for (i = 0; i < out[t].length; i++) byCh[out[t][i][5]].push(out[t][i]);
	for (var ch = 0; ch < 16; ch++) {
		a.push(byCh[ch].length);
		for (i = 0; i < byCh[ch].length; i++) a.push.apply(a, byCh[ch][i].slice(0, ENTRY));
	}
	send.apply(null, a);
	sent[p] = a;
	delete sent[p - 64];
	var gates = 0, notes = [];
	for (t = 0; t < out.length; t++) {
		notes.push([]);
		for (i = 0; i < out[t].length; i++)
			if (out[t][i][1] !== -1 && out[t][i][4] > 0) {
				gates |= 1 << t;
				notes[t].push(out[t][i][3]);
			}
	}
	ahead.push({ step: p, id: silent ? -1 : stepper.id, perf: silent ? stepper.perf() : stepper.perfNow, song: stepper.songNow(),
		gates: gates, notes: notes });
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
		tempTick(s, true);
		takeEdits(-2);
	} else {
		takeLoop();
		takeOn(s, false);
		tempTick(s, false);
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
	if (BRAIN && stepper && tempEndAt >= 0) { // a revert waiting: now
		tempEndAt = -1;
		endTemp(false);
		takeEdits(-1);
	}
	if (BRAIN && stepper) stepper.recRelease();
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
	if (clipHolds()) return showEdits(); // COPY / PASTE / CLEAR: a dial clicked doesn't turn
	if (isSeq() && SEQ_IGNORES.indexOf(String(key)) >= 0) return showEdits(); // dimmed: unused in SEQ
	if (selCount()) {
		// Steps selected: a lockable dial locks the value on them ("L track key cycle"); the
		// others are dimmed (put back if one moves anyway).
		if (!stepper.lockable(String(key))) return showEdits();
		edits["L " + editTrack + " " + key + " " + (cycleEdit ? selCycle : -1)] = value;
		laneFocus = String(key);
		followDial(String(key));
		if (lastStep < 0) takeEdits(-1);
		return;
	}
	// In Cycle edit the dial overrides the value in the selected cycle: "track key cycle".
	var k = editTrack + " " + key + (cycleEdit ? " " + selCycle : "");
	if (dialValue(editTrack, String(key)) === value && edits[k] === undefined) return;
	if (group && !cycleEdit) groupTurn(String(key), value);
	edits[k] = value;
	followDial(String(key));
	if (lastStep < 0) takeEdits(-1); // stopped: no tick to wait for
}

// A plain dial turn with a group: every grouped track moves by the same amount, each clamped
// to its range (a list dial: the same number of positions), as on Rack.
function groupTurn(key, value) {
	function now(t) { var e = edits[t + " " + key]; return e !== undefined ? e : stepper.getParam(t, key); }
	var delta = value - now(editTrack), r = stepper.paramRange(key);
	groupTracks().forEach(function (t) {
		if (t !== editTrack) edits[t + " " + key] = Math.max(r[0], Math.min(r[1], now(t) + delta));
	});
}

// The dials' track (0 = track 1): they show its values.
function editTrackSelect(i) {
	if (!BRAIN) return;
	editTrack = Math.max(0, Math.min(TRACKS - 1, Math.round(i)));
	lockSel = {};
	seqSel = -1;
	if (rec && !isSeq()) setRec(false);
	selCycle = Math.min(selCycle, stepper ? stepper.cycleCount(editTrack) - 1 : 0);
	if (cycleEdit) soloChanged();
	showEdits();
	showPads();
}

// ---- recording and monitoring (brain) --------------------------------------------------

// The song time of the note about to come: "noteTime bars beats units" (units: 480 a beat).
function noteTime(bars, beats, units) {
	noteAt = ((bars - 1) * BEATS_PER_BAR + (beats - 1)) + units / (UNITS_PER_STEP * 4);
}

// A note played into the sequencer's track: "noteIn pitch velocity" (0: off). In SEQ mode it
// sounds on the edit track's receiver (monitoring, REC on or off, as on Rack; not on a CC
// track), and with REC on it's recorded: live while playing, at the cursor while stopped.
function noteIn(pitch, velocity) {
	if (!BRAIN || !stepper) return;
	pitch = Math.round(pitch);
	velocity = Math.round(velocity);
	var t = editTrack, c = cycleEdit ? selCycle : -1;
	if (velocity > 0) {
		if (!isSeq() || stepper.getField(t, "cc")) return;
		var note = stepper.snapNote(t, c, pitch);
		held[pitch] = { note: note, track: t, ch: stepper.channelOf(t) };
		holding[note] = (holding[note] || 0) + 1;
		if (holding[note] === 1) send("mon", held[pitch].ch, note, velocity);
		if (rec) record(true, note, velocity);
		return;
	}
	var h = held[pitch];
	if (!h) return;
	delete held[pitch];
	holding[h.note] = Math.max(0, (holding[h.note] || 1) - 1);
	if (!holding[h.note]) send("mon", h.ch, h.note, 0);
	if (rec) record(false, h.note, 0);
}

// Into the sequence, taken on the next 16th like any edit (playing) or at once (stopped);
// stored with the set once REC goes off: a take is one undo step.
function record(on, note, velocity) {
	var t = editTrack, c = cycleEdit ? selCycle : -1, time = noteAt;
	recorded = true;
	if (lastStep >= 0) {
		ops.push({ desc: "track " + (t + 1) + " recorded note " + note + (on ? " on" : " off"), store: false,
			fn: function () { if (on) stepper.recLiveOn(t, c, note, velocity, time); else stepper.recLiveOff(t, c, note, time); } });
		return;
	}
	ops.push({ desc: "track " + (t + 1) + " step-recorded note " + note + (on ? " on" : " off"), store: false,
		fn: function () { if (on) stepper.recStepOn(t, c, note, velocity); else stepper.recStepOff(t, c, note); } });
	takeEdits(-1);
	showPads();
}

// REC: "recSelect 0/1". Only in SEQ mode (the edit track or edited cycle). Step recording
// starts at the first selected step, else step 1.
function recSelect(v) {
	if (!BRAIN || !stepper) return;
	var want = Number(v) !== 0;
	if (want && !isSeq()) {
		log("REC: the edit track isn't in SEQ mode (SETUP MODE)");
		outlet(5, "rec", 0);
		return;
	}
	setRec(want);
}

function setRec(on) {
	if (rec === on) return;
	rec = on;
	stepper.recRelease();
	if (on) {
		var ks = keysSel();
		stepper.recSetCursor(ks.length ? ks[0] : 0);
		recorded = false;
	} else if (recorded) queueSave();
	outlet(5, "rec", on ? 1 : 0);
	log("REC " + (on ? "on" : "off"));
	showPads();
}

// OVD: "ovdSelect 0/1": recorded notes join a step's notes instead of replacing them.
function ovdSelect(v) {
	if (!BRAIN || !stepper) return;
	overdub = Number(v) !== 0;
	stepper.recOverdub(overdub);
}

// ---- SEQ mode (brain) ------------------------------------------------------------------

// The dials SEQ mode doesn't use: dimmed (Rack's seqIgnores).
var SEQ_IGNORES = ["pulses", "rotate", "voicing", "range"];

// The edited cycle in Cycle edit, else -1: where the SCALE, KEYS, PHRASE and RANDOM
// amounts' reads and edits go for the edit track (the cycle's own copy, as Rack's). The
// rest of a group stays at track level.
function editC() {
	return cycleEdit ? selCycle : -1;
}

// The edit track (the edited cycle in Cycle edit) plays a manual sequence.
function isSeq() {
	return !!stepper && stepper.seqMode(editTrack, cycleEdit ? selCycle : -1);
}

// A plain click on a step pad in SEQ mode: selects just that step for KEYS (green); a lock
// selection ends.
function selectSeqStep(step) {
	var t = editTrack, c = cycleEdit ? selCycle : -1;
	if (step >= stepsOf(t)) return;
	seqSel = stepper.unrotated(t, c, step);
	if (rec && lastStep < 0) stepper.recSetCursor(seqSel); // step recording: the cursor moves there
	lockSel = {};
	showEdits();
	showPads();
}

// The steps KEYS edits: the lock selection, else the SEQ step selected.
function keysSel() {
	var sel = selList();
	return sel.length ? sel : seqSel >= 0 ? [seqSel] : [];
}

// New notes' velocity in SEQ mode: the Velocity dial (the edited cycle's in Cycle edit).
function defaultSeqVelocity() {
	return Math.max(1, Math.min(127, Math.round(dialValue(editTrack, "velocity"))));
}

// KEYS in SEQ mode: the key in or out of the first selected step's chord, every selected
// step taking that chord (keeping its velocity and gate); off the scale only out.
function seqKey(note) {
	var sel = keysSel(), t = editTrack, c = cycleEdit ? selCycle : -1;
	if (!sel.length) return;
	var first = sel[0], st = stepper.seqStep(t, c, first), inChord = st && st.notes.indexOf(note) >= 0;
	if (!inChord && !((stepper.scaleNotes(t, c) >> (note % 12)) & 1)) return;
	var vel = defaultSeqVelocity();
	ops.push({ desc: "track " + (t + 1) + " SEQ note " + note + " on " + sel.length + " steps", store: true, fn: function () {
		stepper.seqToggleNote(t, c, first, note, vel);
		sel.slice(1).forEach(function (pos) { stepper.seqCopyChord(t, c, pos, first, vel); });
	} });
	if (lastStep < 0) takeEdits(-1);
}

// REST: the selected steps lose their notes.
function seqRest() {
	var sel = keysSel(), t = editTrack, c = cycleEdit ? selCycle : -1;
	if (!sel.length) return;
	ops.push({ desc: "track " + (t + 1) + " " + sel.length + " SEQ steps made rests", store: true,
		fn: function () { sel.forEach(function (pos) { stepper.seqClear(t, c, pos); }); } });
	if (lastStep < 0) takeEdits(-1);
}

// ---- step pools (brain) ----------------------------------------------------------------

// EUCLID mode, steps selected for locks: whether the first one has its own pool (KEYS shows
// TRACK POOL).
function stepPoolShown() {
	if (!stepper || isSeq() || !selCount()) return false;
	return stepper.stepPool(editTrack, cycleEdit ? selCycle : -1, selList()[0]).length > 0;
}

// KEYS with steps selected (EUCLID mode): the key in or out of the first selected step's
// pool (the track's to start from), every selected step taking the result: a pool lock, the
// edited cycle's in Cycle edit. Only the edit track's steps, as the other locks.
function stepPoolKey(note) {
	var sel = selList(), t = editTrack, c = cycleEdit ? selCycle : -1;
	if (!sel.length) return;
	ops.push({ desc: "track " + (t + 1) + " step pool note " + note + " on " + sel.length + " steps", store: true, fn: function () {
		if (!stepper.stepPoolKey(t, c, sel[0], note)) return;
		sel.slice(1).forEach(function (pos) { stepper.stepPoolCopy(t, c, pos, sel[0]); });
	} });
	if (lastStep < 0) takeEdits(-1);
}

// TRACK POOL: the selected steps play the track's pool again (their pool locks go).
function trackPool() {
	var sel = selList(), t = editTrack, c = cycleEdit ? selCycle : -1;
	if (!sel.length) return;
	ops.push({ desc: "track " + (t + 1) + " " + sel.length + " steps back to the track pool", store: true,
		fn: function () { sel.forEach(function (pos) { stepper.stepPoolClear(t, c, pos); }); } });
	if (lastStep < 0) takeEdits(-1);
}

// ---- step locks (brain) ----------------------------------------------------------------

// The slow blink of the LOCKS and CYCLE tab frames: on or off now.
function slowBlinkOn() {
	return Math.floor(blinks / 4) % 2 === 0;
}

function selCount() {
	return Object.keys(lockSel).length;
}

function selList() {
	return Object.keys(lockSel).map(Number).sort(function (a, b) { return a - b; });
}

// Ends the selection: the dials show the track's (or cycle's) values again.
function clearLockSel() {
	if (!selCount()) return;
	lockSel = {};
	showEdits();
	showDisplay();
}

// Ctrl+click on a step pad: the step joins the selection, or leaves it.
function selectStep(step) {
	var t = editTrack, c = cycleEdit ? selCycle : -1;
	if (step >= stepsOf(t)) return;
	var pos = stepper.unrotated(t, c, step);
	if (lockSel[pos]) delete lockSel[pos];
	else lockSel[pos] = true;
	log("step " + (step + 1) + (lockSel[pos] ? " selected" : " deselected") + " for locks (" + selCount() + ")");
	showEdits();
	showPads();
}

// LOCKS page STEPS row: "lockStepClick step add". A click selects just that step (the only
// one selected: deselects it); Ctrl+click (add 1) adds it or takes it out, as on the pads.
function lockStepClick(i, add) {
	if (!BRAIN || !stepper) return;
	var step = stepPage * 16 + Math.round(i);
	if (Number(add)) return selectStep(step);
	var t = editTrack, c = cycleEdit ? selCycle : -1;
	if (step >= stepsOf(t)) return;
	var pos = stepper.unrotated(t, c, step), only = selCount() === 1 && lockSel[pos];
	lockSel = {};
	if (!only) lockSel[pos] = true;
	showEdits();
	showPads();
}

// LOCKS page SELECT: NONE, PAGE (the page's steps), CLEAR LOCKS (on the selected steps).
function lockSelect(v) {
	var t = editTrack, c = cycleEdit ? selCycle : -1;
	if (v === 0) clearLockSel();
	else if (v === 1) {
		for (var i = 0; i < 16 && stepPage * 16 + i < stepsOf(t); i++) lockSel[stepper.unrotated(t, c, stepPage * 16 + i)] = true;
		showEdits();
	} else {
		var sel = selList();
		if (!sel.length) return;
		ops.push({ desc: "track " + (t + 1) + " locks cleared on " + sel.length + " steps", store: true,
			fn: function () { sel.forEach(function (pos) { stepper.lockClear(t, c, pos, null); }); } });
		if (lastStep < 0) takeEdits(-1);
	}
	showPads();
}

// LOCKS page lanes: "lane l step value" (0..1 up the lane), "laneClear l step" (Ctrl+click).
function lane(l, step, value) {
	var key = laneKeys[Math.round(l)];
	if (!BRAIN || !stepper || !key || key === "pool") return;
	var t = editTrack, c = cycleEdit ? selCycle : -1, r = laneRange(key);
	var pos = stepper.unrotated(t, c, stepPage * 16 + Math.round(step)), v = r[0] + Number(value) * (r[1] - r[0]);
	if (key === "nvel" || key === "gate") {
		var what = key === "nvel" ? 1 : 2, iv = Math.round(v);
		ops.push({ desc: "track " + (t + 1) + " step " + (pos + 1) + " SEQ " + key + " " + iv, store: true,
			fn: function () { stepper.seqSet(t, c, pos, what, iv); } });
	} else ops.push({ desc: "track " + (t + 1) + " step " + (pos + 1) + " " + key + " locked", store: true,
		fn: function () { stepper.lockSet(t, c, pos, key, v); } });
	if (lastStep < 0) takeEdits(-1);
}

function laneClear(l, step) {
	var key = laneKeys[Math.round(l)];
	if (!BRAIN || !stepper || !key || key === "pool") return;
	var t = editTrack, c = cycleEdit ? selCycle : -1, pos = stepper.unrotated(t, c, stepPage * 16 + Math.round(step));
	if (key === "nvel" || key === "gate") {
		// The SEQ lanes: back to the default (the Velocity dial, one step).
		var what = key === "nvel" ? 1 : 2, dv = key === "nvel" ? defaultSeqVelocity() : 1;
		ops.push({ desc: "track " + (t + 1) + " step " + (pos + 1) + " SEQ " + key + " default", store: true,
			fn: function () { stepper.seqSet(t, c, pos, what, dv); } });
	} else ops.push({ desc: "track " + (t + 1) + " step " + (pos + 1) + " " + key + " lock removed", store: true,
		fn: function () { stepper.lockClear(t, c, pos, key); } });
	if (lastStep < 0) takeEdits(-1);
}

// ---- cycles (brain) --------------------------------------------------------------------

// What a dial shows: the edited cycle's value in Cycle edit, else the track's.
function dialValue(t, key) {
	return cycleEdit ? stepper.getCycleParam(t, selCycle, key) : stepper.getParam(t, key);
}

// The step pads' view of the edit track: the edited cycle's in Cycle edit.
function stepsOf(t) {
	return cycleEdit ? stepper.cycleSteps(t, selCycle) : stepper.steps(t);
}

function playedOf(t) {
	return cycleEdit ? stepper.cyclePlayedSteps(t, selCycle) : stepper.playedSteps(t);
}

function hitsOf(t) {
	return cycleEdit ? stepper.cycleHits(t, selCycle) : stepper.hits(t);
}

// CYCLE page EDIT: "cycleEditSelect 0/1". While on, the CYCLE tab is framed red.
function cycleEditSelect(v) {
	if (!BRAIN || !stepper) return;
	cycleEdit = Number(v) !== 0;
	lockSel = {};
	seqSel = -1;
	log("cycle edit " + (cycleEdit ? "on, cycle " + (selCycle + 1) : "off"));
	soloChanged();
	showEdits();
	showPads();
}

// Picks a cycle (the CYCLE page); one past the last adds cycles, as on Rack.
// TRACK's cycle lanes: a lane's label clicked ("cycleLaneClick c"). In Cycle edit it
// edits that cycle, as the CYCLE page's choice.
function cycleLaneClick(c) {
	if (!BRAIN || !stepper || !cycleEdit) return;
	c = Math.round(c);
	if (c < 0 || c >= stepper.cycleCount(editTrack) || c === selCycle) return;
	selectCycle(c);
}

function selectCycle(c) {
	c = Math.max(0, Math.min(7, Math.round(c)));
	var t = editTrack;
	selCycle = c;
	if (c >= stepper.cycleCount(t)) {
		ops.push({ desc: "track " + (t + 1) + " now " + (c + 1) + " cycles", store: true, fn: function () {
			if (stepper.cycleCount(t) < c + 1) stepper.setCycleCount(t, c + 1);
		} });
	}
	if (cycleEdit) soloChanged();
	if (ops.length && lastStep < 0) takeEdits(-1);
	showEdits();
	showPads();
}

// In Cycle edit the edited cycle plays alone (the edit track's), as on Rack.
function soloChanged() {
	var c = cycleEdit ? selCycle : -1, t = editTrack;
	ops.push({ desc: c < 0 ? "every cycle plays" : "track " + (t + 1) + " cycle " + (c + 1) + " plays alone", store: false,
		fn: function () { for (var i = 0; i < TRACKS; i++) stepper.setSoloCycle(i, i === t ? c : -1); } });
	if (lastStep < 0) takeEdits(-1);
}


// ---- the display (brain) -------------------------------------------------------------

// The view shown: Mute mode's settings over any page, as on Rack, else the page chosen.
function currentView() {
	return padMode === MODE_MUTE ? "MUTE" : gearOpen ? DISPLAY.gear : DISPLAY.pages[dispPage];
}

// The gear right of the page tabs: the DEVICE page, the device's own settings. While it
// shows, the page tabs are their button-mode copy (no page chosen) and the gear is lit.
var gearOpen = false, gearShown = null;
function gearSelect() {
	if (!BRAIN || !DISPLAY) return;
	gearOpen = true;
	showGear();
	showView();
}

function showGear() {
	if (gearShown === gearOpen) return;
	gearShown = gearOpen;
	outlet(6, "script", gearOpen ? "hide" : "show", "d_pages");
	outlet(6, "script", gearOpen ? "show" : "hide", "d_pagesb");
	outlet(6, "script", gearOpen ? "hide" : "show", "d_gear");
	outlet(6, "script", gearOpen ? "show" : "hide", "d_gear_on");
	if (!gearOpen) outlet(6, "d_pages", dispPage);
}

// The controls a view shows: its rows' (each a label and a control), or the note for a
// page not built yet; SETUP also the device's own controls.
function viewItems(view) {
	var rows = DISPLAY.views[view], out = [];
	if (!rows) return ["d_soon"];
	rows.forEach(function (r) {
		if (r.showWhen || r.kind === "toggle") return; // shown by showDisplay when it applies
		if (r.label) out.push("dl_" + r.name);
		out.push("d_" + r.name);
	});
	if (view === DISPLAY.gear) out.push("dl_instance", "d_instance", "dl_lookahead", "d_lookahead", "d_status");
	if (DISPLAY.diagrams && DISPLAY.diagrams[view]) out.push("d_diagram");
	if (DISPLAY.boards && DISPLAY.boards[view]) out.push("d_board");
	return out;
}

// Shows the current view's controls, hiding the last view's, and sets them.
function showView() {
	if (!BRAIN || !DISPLAY) return;
	var view = currentView();
	if (view !== shownView) {
		if (shownView !== null) {
			viewItems(shownView).forEach(function (n) { outlet(6, "script", "hide", n); });
			(DISPLAY.views[shownView] || []).forEach(function (r) {
				if (r.kind === "toggle" && dispShown["tg_" + r.name]) outlet(6, "script", "hide", "d_" + r.name + (dispShown["tg_" + r.name] === 2 ? "_on" : ""));
				else if (r.showWhen && dispShown["vis_" + r.name]) outlet(6, "script", "hide", "d_" + r.name);
			});
		}
		viewItems(view).forEach(function (n) { outlet(6, "script", "show", n); });
		shownView = view;
		dispShown = { rndmark: dispShown.rndmark, cycmark: dispShown.cycmark, stepmark: dispShown.stepmark, playbar: dispShown.playbar };
	}
	showDisplay();
}

// The page tabs: "displayPage i". A tab while in Mute mode leaves it, back to the pad mode
// before it (staged mutes dropped), as on Rack.
function displayPage(i) {
	if (!BRAIN || !DISPLAY) return;
	dispPage = Math.max(0, Math.min(DISPLAY.pages.length - 1, Math.round(i)));
	gearOpen = false;
	showGear();
	if (padMode === MODE_MUTE) {
		padModeSelect(preMuteMode);
		outlet(5, "mode", padMode);
	}
	showView();
}

// A dial turned: the display shows its page (Rack's "display follows knobs"). In Mute mode
// the Mute view stays; the page is there once Mute mode is left.
function followDial(key) {
	if (!DISPLAY || !DISPLAY.dialPages) return;
	var i = DISPLAY.pages.indexOf(selCount() && stepper.lockable(key) ? "LOCKS" : DISPLAY.dialPages[key]);
	if (i < 0 || (i === dispPage && !gearOpen)) return;
	dispPage = i;
	gearOpen = false;
	showGear();
	outlet(6, "d_pages", i);
	showView();
}

function rowOf(name) {
	var rows = DISPLAY ? DISPLAY.views[currentView()] : null;
	for (var i = 0; rows && i < rows.length; i++) if (rows[i].name === name) return rows[i];
	return null;
}

// What a row's control shows: the edit track's setting, a sequencer setting, the playing
// pattern (PLAY); null for buttons.
function rowValue(row) {
	var v;
	if (row.field) v = stepper.getField(editTrack, row.field, editC());
	else if (row.setting) v = stepper.getSetting(row.setting);
	else if (row.random) v = Math.round(stepper.getRandom(editTrack, row.random, row.param, editC()));
	else if (row.action === "queue") v = perfShown().current;
	else if (row.action === "cycleCount") v = stepper.cycleCount(editTrack) - 1;
	else if (row.action === "cycleSelect") v = selCycle;
	else if (row.action === "cycleMute") v = stepper.cycleState(editTrack, selCycle).muted ? 1 : 0;
	else if (row.action === "cycleEdit") v = cycleEdit ? 1 : 0;
	else if (row.action === "lanePage") v = lanePage;
	else if (row.action === "seqMode") v = isSeq() ? 1 : 0;
	else if (row.action === "stepPage") v = stepPage;
	else if (row.action === "follow") v = follow ? 1 : 0;
	else if (row.action === "tempTiming") v = tempTiming;
	else if (row.action === "songOn") v = stepper.song().on ? 1 : 0;
	else if (row.action === "songLoop") v = stepper.song().loop ? 1 : 0;
	else if (row.action === "songPage") v = songPage;
	else if (row.action === "songSelect") v = songSel - songPage * SONG_PAGE;
	else if (row.action === "songSlot" || row.action === "songBars") {
		var en = stepper.song().entries[songSel];
		v = !en ? (row.action === "songBars" ? 1 : 0) : row.action === "songBars" ? en.bars : en.slot;
	}
	else return null;
	if (row.values) v = Math.max(0, row.values.indexOf(v));
	return v;
}

// Sets the current view's controls from the engine: only what changed.
function showDisplay() {
	if (!BRAIN || !DISPLAY || !stepper) return;
	var rows = DISPLAY.views[currentView()] || [], used = null;
	if (currentView() === "LOCKS") resolveLanePage(); // before LANES shows which
	rows.forEach(function (row) {
		if (row.kind === "toggle") {
			// Two buttons: d_<name> (off) or d_<name>_on (on) shown, or neither.
			var tg = row.showWhen && !showsNow(row) ? 0 : rowValue(row) ? 2 : 1, was = dispShown["tg_" + row.name] || 0;
			if (tg !== was) {
				dispShown["tg_" + row.name] = tg;
				if (was) outlet(6, "script", "hide", "d_" + row.name + (was === 2 ? "_on" : ""));
				if (tg) outlet(6, "script", "show", "d_" + row.name + (tg === 2 ? "_on" : ""));
			}
			return;
		}
		if (row.showWhen) {
			var vis = showsNow(row);
			if (dispShown["vis_" + row.name] !== vis) {
				dispShown["vis_" + row.name] = vis;
				outlet(6, "script", vis ? "show" : "hide", "d_" + row.name);
			}
			if (!vis) return;
		}
		if (row.kind === "text") {
			var line = textOf(row);
			if (dispShown["d_" + row.name] !== line) {
				dispShown["d_" + row.name] = line;
				outlet(6, "d_" + row.name, line);
			}
			return;
		}
		if (row.marks) {
			// Renamed choices: pattern slots holding a pattern read "3*", cycles with
			// changes "3*" and muted ones "(3)", CLEAR lists what the cycle changes.
			// Renaming may reset what the control shows: it's set again below.
			var names;
			if (row.marks === "used") {
				if (used === null) used = perfShown().used;
				names = row.items.map(function (n, k) { return used & (1 << k) ? n + "*" : n; });
			} else if (row.marks === "cycles") {
				names = row.items.map(function (n, k) {
					var st = stepper.cycleState(editTrack, k), m = st.modified ? n + "*" : n;
					return st.muted ? "(" + m + ")" : m;
				});
			} else if (row.marks === "lanePages") names = lanePageNames();
			else if (row.marks === "stepPages") names = row.items.slice(0, pageCount());
			else if (row.marks === "songPages") names = row.items.slice(0, songPages());
			else if (row.marks === "songEntries") names = songCellNames(0);
			else if (row.marks === "songCounts") names = songCellNames(1);
			else names = cycleClearEntries().map(function (e) { return e.name; });
			if (dispShown["dr_" + row.name] !== names.join(" ")) {
				dispShown["dr_" + row.name] = names.join(" ");
				delete dispShown["d_" + row.name];
				relabelled[row.name] = Date.now();
				outlet(6, ["dr_" + row.name].concat(names));
			}
		}
		var v = rowValue(row), key = "d_" + row.name;
		if (v !== null && dispShown[key] !== v) {
			dispShown[key] = v;
			outlet(6, key, v);
		}
		if (row.alt) {
			var altOn = row.altWhen === "@cycleEdit" ? cycleEdit : row.altWhen === "@temp" ? !!tempShown
				: stepper.getField(editTrack, row.altWhen);
			var text = altOn ? row.alt : row.label;
			if (dispShown["dl_" + row.name] !== text) {
				dispShown["dl_" + row.name] = text;
				outlet(6, "dl_" + row.name, text);
			}
		}
		if (row.activeWhen || row.seqUnused) {
			var on = row.seqUnused && isSeq() ? 0
				: row.activeWhen === "@songEntry" ? (songSel < stepper.song().count ? 1 : 0)
				: !row.activeWhen || row.activeWhen["in"].indexOf(stepper.getField(editTrack, row.activeWhen.field, editC())) >= 0 ? 1 : 0;
			if (dispShown["da_" + row.name] !== on) {
				dispShown["da_" + row.name] = on;
				outlet(6, "da_" + row.name, on);
			}
		}
	});
	showDiagram();
	showBoard();
	showRandomMark();
}

// The diagram under the rows, as Rack's display draws it: sent when it changes, its
// playhead on each 16th while playing.
function showDiagram() {
	var which = DISPLAY.diagrams ? DISPLAY.diagrams[currentView()] : null;
	if (!which) return;
	var t = editTrack, c = editC(), msg, kind = 0;
	if (which === "groove") {
		var percussion = stepper.getField(t, "groove") < 4, loops = stepper.getField(t, "grooveLoop");
		var count = percussion ? 1 : [1, 2, 4, 8, 16, 32, 64][stepper.getField(t, "grooveLength")];
		var label = percussion ? (loops ? "1 BAR, FROM EACH LOOP START" : "1 BAR, REPEATED") : axisLabel(count, loops);
		msg = ["d_diagram", "groove", label, stepper.getParam(t, "accent") / 100].concat(rounded(stepper.diagram(t, 0)));
	} else if (which === "phrase") {
		var pc = [1, 2, 4, 8, 16, 32, 64][stepper.getField(t, "phraseLength", c)], pl = stepper.getField(t, "phraseLoop", c);
		kind = 1;
		if (stepper.getField(t, "phrase", c) === 9) msg = ["d_diagram", "grid", axisLabel(pc, pl)].concat(stepper.diagram(t, 3, c));
		else msg = ["d_diagram", "notes", axisLabel(pc, pl), 0].concat(stepper.diagram(t, 1, c));
	} else if (which === "scale") {
		kind = -1;
		msg = ["d_diagram", "scalekeys", stepper.scaleNotes(t, c), stepper.getField(t, "root", c)];
	} else {
		kind = -1;
		msg = ["d_diagram", "notes", "NEXT 16 NOTES", 1].concat(stepper.diagram(t, 2, c));
	}
	var key = msg.join(" ");
	if (dispShown.d_diagram !== key) {
		dispShown.d_diagram = key;
		outlet(6, msg);
	}
	var x = kind < 0 || lastStep < 0 ? -1 : Math.round(stepper.diagramPhase(t, kind, lastStep / 4 + 1e-6, c) * 400) / 400;
	if (dispShown.d_play !== x) {
		dispShown.d_play = x;
		outlet(6, "d_diagram", "play", x);
	}
}

// The bar under the playing cycle on CYCLE's CYCLE row: shown while playing, on that page.
function showPlayBars() {
	var want = null;
	if (currentView() === "CYCLE" && lastStep >= 0 && on && onIn === 0)
		want = "d_cycleSelect_play" + stepper.cycleAt(editTrack, lastStep / 4 + 1e-6);
	else if (currentView() === "SONG" && lastStep >= 0 && on) {
		// SONG: under the entry playing, if it's on the page shown.
		var k = songShown().playing - songPage * SONG_PAGE;
		if (k >= 0 && k < SONG_PAGE) want = "d_songCount_play" + k;
	}
	if (dispShown.playbar === want) return;
	if (dispShown.playbar) outlet(6, "script", "hide", dispShown.playbar);
	if (want) outlet(6, "script", "show", want);
	dispShown.playbar = want;
}

// The blue frame over the RANDOM tab: shown while the edit track has a random amount.
function showRandomMark() {
	var on = false, rows = DISPLAY.views.RANDOM || [];
	for (var i = 0; i < rows.length && !on; i++)
		if (rows[i].random === "amount" && stepper.getRandom(editTrack, "amount", rows[i].param, editC()) !== 0) on = true;
	if (dispShown.rndmark !== on) {
		dispShown.rndmark = on;
		outlet(6, "script", on ? "show" : "hide", "d_rndmark");
	}
	showPlayBars();
	// LOCKS' frame blinks while steps are selected, CYCLE's while Cycle edit is on.
	var stepOn = selCount() > 0 && slowBlinkOn(), cycOn = cycleEdit && slowBlinkOn();
	if (dispShown.stepmark !== stepOn) {
		dispShown.stepmark = stepOn;
		outlet(6, "script", stepOn ? "show" : "hide", "d_stepmark");
	}
	if (dispShown.cycmark !== cycOn) {
		dispShown.cycmark = cycOn;
		outlet(6, "script", cycOn ? "show" : "hide", "d_cycmark");
	}
}

// CYCLE page CLEAR: ALL (every change to the selected cycle), HITS (its step edits), then a
// name per override; Rack's short names. ALL and HITS are always there (HITS does nothing
// without step edits): a live.tab with one choice shows numbers instead.
var PARAM_SHORT = { steps: "STEPS", pulses: "PULSE", rotate: "ROT", division: "DIV", length: "LEN", repeats: "REP",
	ramp: "RAMP", repeatTime: "TIME", pace: "PACE", velocity: "VEL", probability: "PROB", sustain: "SUS", accent: "ACC",
	timing: "TIM", delay: "DLY", pitch: "PITCH", voicing: "VOIC", range: "RANGE" };
function cycleClearEntries() {
	var st = stepper.cycleState(editTrack, selCycle), out = [{ name: "ALL", what: 0 }, { name: "HITS", what: 1 }];
	// Then, as Rack's: its step locks, its own scale / pool / phrase, SEQ sequence, mode,
	// the overrides and the random amounts ("R.PITCH").
	if (st.ownLocks) out.push({ name: "LOCKS", what: 3 });
	if (st.ownTonal) out.push({ name: "SCALE", what: 4 });
	if (st.ownSeq) out.push({ name: "SEQ", what: 5 });
	if (st.ownMode) out.push({ name: "MODE", what: 6 });
	st.overrides.forEach(function (k) { out.push({ name: PARAM_SHORT[k] || k, what: 2, key: k }); });
	st.randomOverrides.forEach(function (k) { out.push({ name: "R." + (PARAM_SHORT[k] || k), what: 7, key: k }); });
	return out;
}

// A text row's line.
function textOf(row) {
	if (row.text === "trackInfo") return cycleEdit ? stepper.cycleInfo(editTrack, selCycle) : stepper.trackInfo(editTrack);
	if (row.text === "keysHelp") {
		if (!isSeq()) {
			if (!selCount()) return "CLICK = ADD / REMOVE A POOL NOTE";
			return stepPoolShown() ? "THE STEPS' OWN POOL: CLICK = ADD / REMOVE"
				: "STEPS PLAY THE TRACK POOL: CLICK = THEIR OWN";
		}
		var ks = keysSel();
		if (!ks.length) return "SEQ: CLICK A STEP PAD TO SEE / EDIT ITS NOTES";
		return stepper.seqStep(editTrack, cycleEdit ? selCycle : -1, ks[0]) ? "SEQ: THE STEP'S CHORD. CLICK = ADD / REMOVE A NOTE"
			: "SEQ: A REST. CLICK = GIVE IT A NOTE";
	}
	if (row.text === "songInfo") return songInfo();
	if (row.text === "cyclePlaying") {
		var n = stepper.cycleCount(editTrack);
		if (cycleEdit) return "EDIT: CYCLE " + (selCycle + 1) + " PLAYS ALONE; THE DIALS AND STEP PADS EDIT IT";
		if (lastStep >= 0 && on && onIn === 0) return "PLAYING: CYCLE " + (stepper.cycleAt(editTrack, lastStep / 4 + 1e-6) + 1) + " OF " + n;
		return n === 1 ? "1 CYCLE" : n + " CYCLES, A LOOP EACH IN TURN";
	}
	return "";
}

// LOCKS' board: the step page's 16 steps as an edit row, then a lane per locked parameter
// (up to 6), each step's lock as a bar.
var MAX_LANES = 6;
function lanesMessage(t) {
	var c = cycleEdit ? selCycle : -1, n = stepsOf(t), played = playedOf(t), hits = hitsOf(t), first = stepPage * 16;
	var now = playingStep(), cells = [], pos = [];
	for (var i = 0; i < 16; i++) {
		var step = first + i;
		pos.push(step < n ? stepper.unrotated(t, c, step) : -1);
		cells.push(step >= n ? 0 : 1 + (hits[step] ? 2 : 0) + (lockSel[pos[i]] ? 4 : 0) + (stepper.locksAt(t, c, pos[i]) ? 8 : 0) + (step >= played ? 16 : 0));
	}
	var used = resolveLanePage();
	laneKeys = used.slice(lanePage * MAX_LANES, (lanePage + 1) * MAX_LANES);
	var msg = ["d_board", "lanes", cycleEdit ? "CYCLE " + (selCycle + 1) : "STEPS", now >= first && now < first + 16 ? now - first : -1].concat(cells, [laneKeys.length]);
	laneKeys.forEach(function (key) {
		var r = laneRange(key);
		msg.push(key === "nvel" ? "N.VEL" : key === "gate" ? "GATE" : key === "velocity" && stepper.getField(t, "cc") ? "VALUE" : key === "pool" ? "POOL" : PARAM_SHORT[key] || key, r[0], r[1]);
		for (var k = 0; k < 16; k++) {
			var v = pos[k] >= 0 ? laneValue(t, c, pos[k], key) : null;
			msg.push(v === null ? "x" : Math.round(v * 100) / 100);
		}
	});
	msg.push(used.length > (lanePage + 1) * MAX_LANES ? 1 : 0);
	return msg;
}

// Which 6 lanes LOCKS shows: those of the parameter a dial just locked, else the group
// chosen (kept in range). Returns the lanes: in SEQ mode the steps' own velocity and gate
// ("nvel", "gate") first, then the locked parameters, then "pool" if a step has its own
// pool (not a dial's value: its steps just marked, not editable there).
function resolveLanePage() {
	var c = cycleEdit ? selCycle : -1, used = stepper.locksUsed(editTrack, c);
	if (isSeq()) used = ["nvel", "gate"].concat(used);
	if (stepper.poolsUsed(editTrack, c)) used = used.concat(["pool"]);
	if (laneFocus !== null && used.indexOf(laneFocus) >= 0) {
		lanePage = Math.floor(used.indexOf(laneFocus) / MAX_LANES);
		laneFocus = null;
	}
	lanePage = Math.max(0, Math.min(lanePage, Math.ceil(used.length / MAX_LANES) - 1));
	return used;
}

// A lane's range and a step's value in it: the SEQ lanes (velocity 1-127, gate 1-16 steps:
// steps with notes only), else a parameter's lock.
function laneRange(key) {
	if (key === "pool") return [0, 0]; // lo = hi: a lane of marks
	return key === "nvel" ? [1, 127] : key === "gate" ? [1, 16] : stepper.paramRange(key);
}

function laneValue(t, c, pos, key) {
	if (key === "pool") return stepper.stepPool(t, c, pos).length ? 1 : null;
	if (key === "nvel" || key === "gate") {
		var st = stepper.seqStep(t, c, pos);
		return st ? (key === "nvel" ? st.velocity : st.gate) : null;
	}
	return stepper.lockGet(t, c, pos, key);
}

// TRACK with several cycles, as Rack's: the step page as a row for what the pads edit (the
// track, or EDIT n: the edited cycle), then a lane per cycle with the same steps as that
// cycle plays them. Cells as LOCKS' (1 + 2 hit + 4 selected + 8 locked + 16 cut off); a
// lane's flags: 1 playing, 2 muted, 4 selected; its label "C2*" while it plays differently.
function cycleLanesMessage(t) {
	var first = stepPage * 16, now = playingStep(), n = stepper.cycleCount(t);
	var playCycle = now >= 0 ? stepper.cycleAt(t, lastStep / 4 + 1e-6) : -1;
	var row = function (c, sel) {
		var steps = c >= 0 ? stepper.cycleSteps(t, c) : stepper.steps(t);
		var played = c >= 0 ? stepper.cyclePlayedSteps(t, c) : stepper.playedSteps(t);
		var hits = c >= 0 ? stepper.cycleHits(t, c) : stepper.hits(t), cells = [];
		for (var i = 0; i < 16; i++) {
			var step = first + i;
			if (step >= steps) { cells.push(0); continue; }
			var pos = stepper.unrotated(t, c, step);
			cells.push(1 + (hits[step] ? 2 : 0) + (sel && lockSel[pos] ? 4 : 0) + (stepper.locksAt(t, c, pos) ? 8 : 0) + (step >= played ? 16 : 0));
		}
		return cells;
	};
	var at = function (c) { return now >= first && now < first + 16 && c === playCycle ? now - first : -1; };
	var ec = editC(), editNow = !cycleEdit || selCycle === playCycle ? (now >= first && now < first + 16 ? now - first : -1) : -1;
	var msg = ["d_board", "cycles", cycleEdit ? "EDIT " + (selCycle + 1) : "TRACK", cycleEdit ? 1 : 0, editNow].concat(row(ec, true), [n]);
	for (var c = 0; c < n; c++) {
		var st = stepper.cycleState(t, c);
		msg.push("C" + (c + 1) + (st.modified ? "*" : ""), (c === playCycle ? 1 : 0) + (st.muted ? 2 : 0) + (c === selCycle ? 4 : 0), at(c));
		msg = msg.concat(row(c, false));
	}
	return msg;
}

// The LANES choices when more than 6 parameters are locked: "1-6", "7-12", "13".
function lanePageNames() {
	var n = resolveLanePage().length, out = [];
	for (var i = 0; i * MAX_LANES < n; i++) {
		var a = i * MAX_LANES + 1, b = Math.min(n, (i + 1) * MAX_LANES);
		out.push(a === b ? String(a) : a + "-" + b);
	}
	return out;
}

function showsNow(row) {
	if (row.showWhen === "lanePages") return resolveLanePage().length > MAX_LANES;
	if (row.showWhen === "stepPages") return pageCount() > 1;
	if (row.showWhen === "seqStep") return isSeq() && keysSel().length > 0;
	if (row.showWhen === "stepPool") return stepPoolShown();
	if (row.showWhen === "songOverridden") return songShown().overridden;
	if (row.showWhen === "songPages") return songPages() > 1;
	return true;
}

// The board under TRACK's and KEYS' first row: the steps, or the keyboard.
function showBoard() {
	var which = DISPLAY.boards ? DISPLAY.boards[currentView()] : null;
	if (!which) return;
	var t = editTrack, msg;
	if (which === "lanes") msg = lanesMessage(t);
	else if (which === "steps" && stepper.cycleCount(t) > 1) msg = cycleLanesMessage(t);
	else if (which === "steps") {
		var n = stepsOf(t), sc = cycleEdit ? selCycle : -1;
		var hits = hitsOf(t).slice(0, n).map(function (h, i) {
			var pos = stepper.unrotated(t, sc, i);
			return (h ? 1 : 0) + (stepper.locksAt(t, sc, pos) ? 2 : 0) + (lockSel[pos] ? 4 : 0);
		});
		msg = ["d_board", "steps", n, playedOf(t), playingStep()].concat(hits);
	} else {
		var first = 12 + 12 * keysOctave, pool = stepper.pool(t, editC()), own = 0;
		if (!isSeq() && selCount()) {
			// Steps selected: the first one's pool, its own (blue) or the track's it starts from.
			var sp = stepper.stepPool(t, cycleEdit ? selCycle : -1, selList()[0]);
			if (sp.length) { pool = sp; own = 1; }
		} else if (isSeq()) {
			// No pool in SEQ mode: the first selected step's chord.
			var ks = keysSel(), st = ks.length ? stepper.seqStep(t, cycleEdit ? selCycle : -1, ks[0]) : null;
			pool = st ? st.notes : [];
		}
		var lit = lastStep >= 0 && playing && playing.notes ? playing.notes[t] : [];
		msg = ["d_board", "keys", NOTE_NAMES[0] + (first / 12 - 1) + " - B" + (first / 12 + 2), first, 4,
			stepper.scaleNotes(t, editC()), stepper.getField(t, "root", editC()), pool.length].concat(pool, [lit.length], lit, [own]);
	}
	var key = msg.join(" ");
	if (dispShown.d_board !== key) {
		dispShown.d_board = key;
		outlet(6, msg);
	}
}

var NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

// KEYS: a key click, "poolKey note"; SCALE's notes: "scaleKey pc". Taken on the next 16th.
function poolKey(note) {
	if (!BRAIN || !stepper) return;
	var t = editTrack;
	note = Math.round(note);
	if (isSeq()) return seqKey(note);
	if (selCount()) return stepPoolKey(note);
	var ts = groupTracks(), c = editC();
	ops.push({ desc: "track " + (t + 1) + (c >= 0 ? " cycle " + (c + 1) : "") + " pool note " + note, store: true, fn: function () {
		stepper.poolKey(t, note, c);
		// A group: each grouped track's pool takes the note, or loses it, as the edit track's
		// did (the others' own pools: Cycle edit is the edit track's).
		var want = stepper.pool(t, c).indexOf(note) >= 0;
		ts.forEach(function (g) { if (g !== t && (stepper.pool(g).indexOf(note) >= 0) !== want) stepper.poolKey(g, note); });
	} });
	if (lastStep < 0) takeEdits(-1);
}

function scaleKey(pc) {
	if (!BRAIN || !stepper) return;
	var t = editTrack;
	pc = Math.round(pc);
	var ts = groupTracks(), c = editC();
	ops.push({ desc: "track " + (t + 1) + (c >= 0 ? " cycle " + (c + 1) : "") + " scale note " + NOTE_NAMES[pc], store: true, fn: function () {
		stepper.scaleKey(t, pc, c);
		var want = (stepper.scaleNotes(t, c) >> pc) & 1;
		ts.forEach(function (g) { if (g !== t && ((stepper.scaleNotes(g) >> pc) & 1) !== want) stepper.scaleKey(g, pc); });
	} });
	if (lastStep < 0) takeEdits(-1);
}

function axisLabel(count, loops) {
	return count + " " + (loops ? "LOOP" : "BAR") + (count === 1 ? "" : "S");
}

function rounded(values) {
	return values.map(function (v) { return Math.round(v * 1000) / 1000; });
}

// PHRASE's User shape: a click or drag on the grid, "phraseGrid col level". Taken on the
// next 16th like a dial.
function phraseGrid(col, level) {
	if (!BRAIN || !stepper) return;
	var t = editTrack;
	col = Math.round(col);
	level = Math.round(level);
	var ts = groupTracks(), c = editC();
	ops.push({ desc: "track " + (t + 1) + " phrase column " + (col + 1) + " at " + level, store: true,
		fn: function () { ts.forEach(function (g) { stepper.setPhraseLevel(g, col, level, g === t ? c : -1); }); } });
	if (lastStep < 0) takeEdits(-1);
}

// A page setting with a group: the same setting on every grouped track (not the CYCLE,
// LOCKS, PATTERN or SONG pages', nor the sequencer's own settings); null otherwise.
function groupRowOp(row, v) {
	var ts = groupTracks(), f, what = row.field || row.random || row.action, et = editTrack, ec = editC();
	var cOf = function (t) { return t === et ? ec : -1; }; // Cycle edit: the edit track's cycle only
	if (row.field) f = function (t) { stepper.setField(t, row.field, v, cOf(t)); };
	else if (row.random === "locked")
		f = function (t) { if (Math.round(stepper.getRandom(t, "locked")) !== v) stepper.randomAction(t, 1, Math.max(0, changeTime(false))); };
	else if (row.random) f = function (t) { stepper.setRandom(t, row.random, row.param, v, cOf(t)); };
	else if (row.randomAction) {
		var a = row.randomAction[Math.max(0, Math.min(row.randomAction.length - 1, v))];
		what = row.items[row.randomAction.indexOf(a)];
		f = function (t) { stepper.randomAction(t, a, Math.max(0, changeTime(false)), cOf(t)); };
	} else if (row.action === "seqMode" && !cycleEdit) {
		var vel = defaultSeqVelocity();
		f = function (t) { stepper.setSeqMode(t, -1, v === 1, vel); };
	} else return null;
	return { desc: "tracks " + ts.map(function (t) { return t + 1; }).join(" ") + " " + what + " " + v, store: true,
		fn: function () { ts.forEach(f); } };
}

// A display control: "disp name value". Settings are taken on the next 16th like a dial,
// and stored with the set.
function disp(name, value) {
	if (!BRAIN || !stepper) return;
	var row = rowOf(String(name)), v = Math.round(Number(value));
	if (row && (row.kind === "button" || row.kind === "toggle")) v = 0; // a live.text button: "bang"
	if (!row || isNaN(v)) return;
	// Just renamed (marks): what comes back now is the control's echo, not a click.
	if (relabelled[row.name] !== undefined && Date.now() - relabelled[row.name] < 100) return;
	if (row.values) v = row.values[Math.max(0, Math.min(row.values.length - 1, v))];
	var gop = group ? groupRowOp(row, v) : null;
	if (gop) {
		if (row.action === "seqMode") {
			lockSel = {};
			seqSel = -1;
			if (!v && rec) setRec(false);
		}
		ops.push(gop);
		if (lastStep < 0) takeEdits(-1);
		return;
	}
	var t = editTrack, op = null, ec = editC();
	if (row.field) op = { desc: "track " + (t + 1) + " " + row.field + " " + v, fn: function () { stepper.setField(t, row.field, v, ec); } };
	else if (row.random === "locked") {
		// LOCK: a click on the state it's in does nothing; otherwise lock where the sequences
		// are now (or unlock).
		if (v === Math.round(stepper.getRandom(t, "locked"))) return;
		op = { desc: "track " + (t + 1) + " random " + (v ? "locked" : "unlocked"),
			fn: function () { stepper.randomAction(t, 1, Math.max(0, changeTime(false))); } };
	}
	else if (row.random) op = { desc: "track " + (t + 1) + " random " + row.random + (row.param ? " " + row.param : "") + " " + v,
		fn: function () { stepper.setRandom(t, row.random, row.param, v, ec); } };
	else if (row.randomAction) {
		var a = row.randomAction[Math.max(0, Math.min(row.randomAction.length - 1, v))];
		op = { desc: "track " + (t + 1) + " random " + row.items[row.randomAction.indexOf(a)],
			fn: function () { stepper.randomAction(t, a, Math.max(0, changeTime(false)), ec); } };
	}
	else if (row.setting) op = { desc: row.setting + " " + v, fn: function () { stepper.setSetting(row.setting, v); } };
	else if (row.action === "queue") return patternPad(v, false, false);
	else if (row.action === "cycleCount") {
		var n = v + 1;
		selCycle = Math.min(selCycle, n - 1);
		op = { desc: "track " + (t + 1) + " " + n + " cycles", fn: function () { stepper.setCycleCount(t, n); } };
	}
	else if (row.action === "cycleSelect" && armed) {
		// COPY / PASTE / CLEAR: the cycle clicked; the selection stays.
		clipCtrl = mods.ctrl;
		mods = { shift: false, ctrl: false, alt: false };
		clipCycle(v);
		delete dispShown.d_cycleSelect;
		return showDisplay();
	}
	else if (row.action === "cycleSelect") return selectCycle(v);
	else if (row.action === "cycleEdit") return cycleEditSelect(v);
	else if (row.action === "lockSelect") return lockSelect(v);
	else if (row.action === "lanePage") {
		lanePage = v;
		return showDisplay();
	}
	else if (row.action === "seqRest") return seqRest();
	else if (row.action === "trackPool") return trackPool();
	else if (row.action === "seqMode") {
		if ((v === 1) === isSeq()) return;
		var vel = defaultSeqVelocity(), mc = cycleEdit ? selCycle : -1;
		lockSel = {};
		seqSel = -1;
		if (!v && rec) setRec(false);
		op = { desc: "track " + (t + 1) + (mc >= 0 ? " cycle " + (mc + 1) : "") + (v ? " SEQ" : " EUCLID") + " mode",
			fn: function () { stepper.setSeqMode(t, mc, v === 1, vel); } };
	}
	else if (row.action === "stepPage") return stepPageSelect(v);
	else if (row.action === "tempTiming") { // DEVICE's TEMP OFF: stored with the set (the bank's "ui")
		tempTiming = v;
		queueSave();
		return showDisplay();
	}
	else if (row.action === "follow") { // TRACK's FOLLOW: a click flips it; the transport's too
		follow = !follow;
		outlet(5, "follow", follow ? 1 : 0);
		return showPads();
	}
	else if (String(row.action).indexOf("song") === 0) return songAction(row, v);
	else if (row.action === "cycleMute") {
		var mc = selCycle;
		op = { desc: "track " + (t + 1) + " cycle " + (mc + 1) + (v ? " muted" : " unmuted"), fn: function () { stepper.setCycleMuted(t, mc, v); } };
	}
	else if (row.action === "cycleClear") {
		var e = cycleClearEntries()[v], cc = selCycle;
		if (!e) return;
		op = { desc: "track " + (t + 1) + " cycle " + (cc + 1) + " cleared: " + e.name, fn: function () { stepper.cycleClear(t, cc, e.what, e.key); } };
	}
	else if (row.action === "octave") {
		keysOctave = Math.max(0, Math.min(5, keysOctave + (v === 0 ? -1 : 1)));
		return showDisplay();
	}
	else if (row.action === "copy") op = { desc: "pattern copied to " + (v + 1), fn: function () { stepper.copyPattern(v); } };
	else if (row.action === "clear") op = { desc: "pattern " + (v + 1) + " cleared", fn: function () { stepper.clearPattern(v); } };
	if (!op) return;
	op.store = true;
	ops.push(op);
	if (lastStep < 0) takeEdits(-1);
}

// ---- the song (SONG page, brain) -------------------------------------------------------
//
// The song follows Live's timeline: its bar 1 is Live's bar 1, and wherever Live plays (a
// loop wrapping, Play from elsewhere) the song plays what it has there. A pattern switch of
// the player's (a pad, the PATTERN page) takes over until BACK TO SONG; past the end without
// LOOP the sequencer is silent. Edits are taken on the next 16th like a dial's, heard from
// the next bar line, and stored with the set.

// Pages of 16 entries, with room for the "+" cell after the last one.
function songPages() {
	return Math.min(Math.floor(stepper.song().count / SONG_PAGE) + 1, SONG_MAX / SONG_PAGE);
}

// The entry cells of the page shown, the patterns' row (0: "P3") or the bars' (1: "x4");
// "+" after the last entry, "-" past it.
function songCellNames(what) {
	var song = stepper.song(), out = [];
	for (var k = 0; k < SONG_PAGE; k++) {
		var i = songPage * SONG_PAGE + k, e = song.entries[i];
		out.push(e ? (what ? "x" + e.bars : "P" + (e.slot + 1)) : i === song.count && i < SONG_MAX ? "+" : "-");
	}
	return out;
}

// The selection after the song changed: kept in range, its page shown.
function songSelect(i) {
	var count = stepper.song().count;
	songSel = Math.max(0, Math.min(i, Math.min(count, SONG_MAX - 1)));
	songPage = Math.floor(songSel / SONG_PAGE);
}

function songNowTime() {
	return lastStep < 0 ? -1 : changeTime(false);
}

// An edit of the song, taken on the next 16th; `after` runs once it's made (the selection).
function songOp(desc, fn, after) {
	ops.push({ desc: "song: " + desc, store: true, fn: function () {
		fn(songNowTime());
		if (after) after();
	} });
	if (lastStep < 0) takeEdits(-1);
	else showDisplay();
}

// A pattern pad on the SONG page (Pattern mode), ADD and "+": the pattern at the end.
function songAdd(slot) {
	songOp("pattern " + (slot + 1) + " added", function (now) {
		if (stepper.song().count < SONG_MAX) stepper.songInsert(SONG_MAX, slot, 0, now);
	}, function () { songSelect(stepper.song().count - 1); });
}

function songAction(row, v) {
	var song = stepper.song(), sel = songSel, has = sel < song.count;
	switch (row.action) {
		case "songOn":
			if ((v === 1) === song.on) return;
			return songOp(v ? "on" : "off", function (now) { stepper.songSetOn(v === 1, now); });
		case "songLoop":
			if ((v === 1) === song.loop) return;
			return songOp(v ? "loop on" : "loop off", function (now) { stepper.songSetLoop(v === 1, now); });
		case "songResume":
			return songOp("back to the song", function (now) { stepper.songResume(now); });
		case "songPage":
			songSelect(Math.min(v * SONG_PAGE, song.count));
			return showDisplay();
		case "songSelect": {
			var i = songPage * SONG_PAGE + v;
			if (i < song.count) {
				songSel = i;
				return showDisplay();
			}
			delete dispShown.d_songSelect; // "-": the selection stays where it was
			delete dispShown.d_songCount;
			if (i === song.count && i < SONG_MAX) return songAdd(perfShown().current);
			return showDisplay();
		}
		case "songSlot":
			if (!has) return showDisplay();
			return songOp("entry " + (sel + 1) + " plays pattern " + (v + 1), function (now) { stepper.songSet(sel, v, -1, now); });
		case "songBars":
			if (!has || v === song.entries[sel].bars) return;
			return songOp("entry " + (sel + 1) + " " + v + " bars", function (now) { stepper.songSet(sel, -1, v, now); });
		case "songBarsSet": {
			if (!has) return;
			var bars = [1, 2, 4, 8, 16, 32][v] || stepper.songDefaultBars(song.entries[sel].slot);
			return songOp("entry " + (sel + 1) + " " + bars + " bars", function (now) { stepper.songSet(sel, -1, bars, now); });
		}
		case "songEdit":
			if (v === 0) return songAdd(perfShown().current);
			if (v === 1) {
				if (!has || song.count >= SONG_MAX) return;
				var e = song.entries[sel];
				return songOp("entry " + (sel + 1) + " copied", function (now) { stepper.songInsert(sel, e.slot, e.bars, now); });
			}
			if (v === 2) {
				if (!has) return;
				return songOp("entry " + (sel + 1) + " deleted", function (now) { stepper.songRemove(sel, now); },
					function () { songSelect(Math.min(sel, stepper.song().count - 1)); });
			}
			return songOp("cleared", function () { stepper.songClear(); }, function () { songSelect(0); });
	}
}

// SONG's info line.
function songInfo() {
	var s = songShown(), total = s.count + (s.count === 1 ? " ENTRY, " : " ENTRIES, ") + s.bars + (s.bars === 1 ? " BAR" : " BARS");
	if (!s.on) return "SONG OFF   " + total;
	if (!s.count) return "SONG ON: ADD ENTRIES (+, ADD, OR PATTERN PADS IN PATTERN MODE)";
	if (s.overridden) return "A PATTERN SWITCH TOOK OVER: BACK TO SONG RETURNS ON THE NEXT BAR";
	if (s.pending && lastStep >= 0) return "TO THE SONG ON THE NEXT BAR   " + total;
	if (s.over && lastStep >= 0) return "PAST THE SONG'S END: SILENT   " + total;
	if (s.playing >= 0 && lastStep >= 0) {
		var e = s.entries[s.playing], bar = Math.floor((lastStep / 4 - s.start) / 4 + 1e-6) + 1;
		if (e) return "ENTRY " + (s.playing + 1) + "/" + s.count + "   P" + (e.slot + 1) + "   BAR " + Math.max(1, Math.min(bar, e.bars)) +
			"/" + e.bars + "   SONG BAR " + (Math.floor(lastStep / 16) + 1) + (s.loop ? "" : "/" + s.bars);
	}
	return "SONG ON: BAR 1 IS LIVE'S BAR 1   " + total + (s.loop ? ", LOOPED" : "");
}

// ---- the track buttons and groups (brain) ---------------------------------------------
//
// The track buttons along the top: a click selects the track the dials and pages edit and
// ends the group; Ctrl+click adds a track to the group or takes it out, Shift+click adds the
// range from the edit track, as on Rack. A plain dial turn moves every grouped track by the
// same amount (each clamped to its range), and a display page setting goes to every grouped
// track; step pads, locks, the CYCLE page and Cycle edit stay on the edit track. COPY, PASTE
// and CLEAR on a grouped track act on the group.

// The tracks a group edit goes to: the edit track and the group, in order.
function groupTracks() {
	var out = [];
	for (var t = 0; t < TRACKS; t++) if (t === editTrack || (group & (1 << t))) out.push(t);
	return out;
}

// A track button: "trackButton i" (after "padMods").
function trackButton(i) {
	if (!BRAIN || !stepper) return;
	var shift = mods.shift, ctrl = mods.ctrl;
	mods = { shift: false, ctrl: false, alt: false };
	i = Math.max(0, Math.min(TRACKS - 1, Math.round(Number(i))));
	if (armed) {
		clipCtrl = ctrl;
		clipTrack(i); // COPY / PASTE / CLEAR: the track (its group) is the target
		return showTrackButtons();
	}
	if (ctrl) {
		if (i !== editTrack) group ^= 1 << i;
	} else if (shift) {
		for (var t = Math.min(i, editTrack); t <= Math.max(i, editTrack); t++) if (t !== editTrack) group |= 1 << t;
	} else {
		group = 0;
		editTrackSelect(i);
	}
	log("track " + (editTrack + 1) + (group ? ", group " + bitList(group | (1 << editTrack)) : ""));
	showTrackButtons();
}

// The track buttons' colours, as Rack lights them: white on a note, red the edit track, dim
// red the group, blue-grey muted, dim orange holding something other than defaults.
var TRACK_COLOURS = { note: [1, 1, 1], edit: [0.9, 0.22, 0.16], group: [0.45, 0.1, 0.07], muted: [0.12, 0.17, 0.28],
	used: [0.24, 0.14, 0.04], empty: [0.06, 0.06, 0.07] };
var trackKeys = [];
function showTrackButtons() {
	if (!BRAIN || !stepper) return;
	var perf = perfShown(), gates = lastStep >= 0 && playing ? playing.gates : 0;
	for (var t = 0; t < TRACKS; t++) {
		var bit = 1 << t, c = gates & bit ? TRACK_COLOURS.note : t === editTrack ? TRACK_COLOURS.edit
			: group & bit ? TRACK_COLOURS.group : perf.muted & bit ? TRACK_COLOURS.muted
			: stepper.trackUsed(t) ? TRACK_COLOURS.used : TRACK_COLOURS.empty;
		var key = c.join(" ");
		if (trackKeys[t] === key) continue;
		trackKeys[t] = key;
		outlet(5, "trk", t, c[0], c[1], c[2], 1);
	}
}

// ---- COPY / PASTE / CLEAR (brain) ----------------------------------------------------------
//
// As on Rack: one is armed at a time, and the next click on a target acts on the clipboard.
// COPY takes a track (a track button, or a pad in Mute mode), a step (a pad in Step mode), a
// pattern (a pad in Pattern mode), a cycle (the CYCLE page's cells) or a dial's value (a
// click on the dial), then disarms. PASTE pastes on the next matching target and disarms; a
// Ctrl+click on the target keeps it armed for more, until PASTE is clicked again. CLEAR
// clears the next target and disarms (Ctrl+click CLEAR: it stays armed). The clipboard is outside the patterns: it pastes
// across them. Edits are taken on the next 16th like a dial's.

// Where the dials are on the panel (from the patch: "dialGeometry key x y w h ..."), to find
// the dial under a click (a live.dial reports a turn, not a click).
var dialArea = [];
function dialGeometry() {
	var a = Array.prototype.slice.call(arguments);
	dialArea = [];
	for (var i = 0; i + 4 < a.length; i += 5)
		dialArea.push({ key: String(a[i]), x: Number(a[i + 1]), y: Number(a[i + 2]), w: Number(a[i + 3]), h: Number(a[i + 4]) });
}

// A COPY / PASTE / CLEAR click: "clipSelect 0/1/2 value" (after "padMods").
function clipSelect(which) {
	if (!BRAIN || !stepper) return;
	var ctrl = mods.ctrl;
	mods = { shift: false, ctrl: false, alt: false };
	which = Math.round(Number(which));
	if (which === 0) armed = armed === "copy" ? null : "copy";
	else if (which === 1) armed = armed === "paste" || !clip ? null : "paste";
	else if (which === 2) {
		// Ctrl+click arms it to stay armed (also when armed already); a plain click on an
		// armed CLEAR disarms it.
		if (armed === "clear" && !ctrl) armed = null;
		else armed = "clear";
		clearSticky = armed === "clear" && ctrl;
	}
	if (armed !== "clear") clearSticky = false;
	showClip();
}

// The buttons: lit while armed, PASTE dimmed while there's nothing to paste. (What they do
// is in Live's Info View; what an armed one does now goes to the debug log.)
var clipShown = null;
function showClip() {
	if (!BRAIN) return;
	var key = [armed, clip ? clipName() : "", clearSticky].join("|");
	if (key === clipShown) return;
	clipShown = key;
	outlet(5, "copy", armed === "copy" ? 1 : 0);
	outlet(5, "paste", armed === "paste" ? 1 : 0);
	outlet(5, "clear", armed === "clear" ? 1 : 0);
	outlet(5, "pasteactive", clip ? 1 : 0);
	if (armed) log(clipInfo());
}

var CLIP_TARGETS = "A TRACK BUTTON, A DIAL, A PAD, OR A CYCLE (CYCLE PAGE)";
function clipInfo() {
	if (armed === "copy") return "COPY: CLICK " + CLIP_TARGETS;
	if (armed === "clear") return "CLEAR: CLICK " + CLIP_TARGETS + (clearSticky ? "  (STAYS ARMED)" : "");
	var where = clip.kind === "track" ? "TRACK BUTTONS, MUTE PADS OR PATTERN PADS" : clip.kind === "step" ? "STEP PADS"
		: clip.kind === "cycle" ? "CYCLES (CYCLE PAGE)" : clip.kind === "pattern" ? "PATTERN PADS"
		: "TRACK BUTTONS, MUTE PADS OR CYCLES";
	return "PASTE " + clipName() + ": CLICK " + where + "  (CTRL+CLICK: MORE AFTER IT)";
}

function clipName() {
	if (clip.kind === "param") return (PARAM_SHORT[clip.key] || clip.key.toUpperCase()) + " " + Math.round(clip.value * 100) / 100;
	if (clip.kind === "track" && clip.tracks.length > 1) return "TRACKS " + listOf(clip.tracks);
	return clip.kind.toUpperCase() + " " + (clip.source + 1);
}

// After a target took the click: COPY disarms, CLEAR too unless it stays armed, PASTE unless
// the click was a Ctrl+click.
function clipDone() {
	if (armed === "copy" || (armed === "clear" && !clearSticky) || (armed === "paste" && !clipCtrl)) armed = null;
	clipCtrl = false;
	clipClickAt = Date.now();
	if (lastStep < 0) takeEdits(-1);
	showClip();
}

// A clipboard edit, taken on the next 16th.
function clipOp(desc, fn, store) {
	ops.push({ desc: desc, store: store !== false, fn: fn });
}

// A track (a track button, or a pad in Mute mode); a grouped one stands for its group.
// Copied tracks paste from the track clicked on, keeping their spacing (tracks 1, 2 and 4
// pasted on 5: 5, 6 and 8).
function clipTrack(t) {
	var set = group && (t === editTrack || (group & (1 << t))) ? groupTracks() : [t];
	if (armed === "copy") {
		clip = { kind: "track", tracks: set, source: set[0] };
		clipOp("tracks " + listOf(set) + " copied", function () { set.forEach(function (s) { stepper.clip("copy", "track", s); }); }, false);
	} else if (armed === "paste" && clip.kind === "track") {
		var from = clip.tracks, shift = t - from[0];
		clipOp("tracks " + listOf(from) + " pasted from track " + (t + 1), function () {
			from.forEach(function (s) {
				if (s + shift < TRACKS) {
					stepper.clip("paste", "track", s, s + shift);
					trackReplaced(s + shift);
				}
			});
		});
	} else if (armed === "paste" && clip.kind === "param") {
		var key = clip.key, v = clip.value;
		clipOp("tracks " + listOf(set) + " " + key + " " + v + " pasted", function () { set.forEach(function (s) { stepper.setParamByKey(s, key, v); }); });
	} else if (armed === "clear") {
		clipOp("tracks " + listOf(set) + " cleared", function () { set.forEach(function (s) { stepper.clip("clear", "track", s); trackReplaced(s); }); });
	} else return false;
	clipDone();
	return true;
}

function listOf(tracks) {
	return tracks.map(function (t) { return t + 1; }).join(" ");
}

// The edit track's data replaced: the selected cycle in range, no steps selected.
function trackReplaced(t) {
	if (t !== editTrack) return;
	selCycle = Math.min(selCycle, stepper.cycleCount(t) - 1);
	lockSel = {};
	seqSel = -1;
	if (rec && !isSeq()) setRec(false);
}

// A pad in Step mode: its step (on/off, locks, pool; in SEQ mode its notes).
function clipStep(i) {
	var t = editTrack, c = cycleEdit ? selCycle : -1, step = stepPage * 16 + i;
	if (step >= stepsOf(t)) return;
	if (armed === "copy") {
		clip = { kind: "step", source: step };
		clipOp("track " + (t + 1) + " step " + (step + 1) + " copied", function () { stepper.clip("copy", "step", t, c, step); }, false);
	} else if (armed === "paste") {
		if (clip.kind !== "step") return;
		clipOp("step " + (clip.source + 1) + " pasted on track " + (t + 1) + " step " + (step + 1), function () { stepper.clip("paste", "step", t, c, step); });
	} else {
		var pos = stepper.unrotated(t, c, step);
		delete lockSel[pos]; // a cleared step leaves the selection
		if (seqSel === pos) seqSel = -1;
		clipOp("track " + (t + 1) + " step " + (step + 1) + " cleared", function () { stepper.clip("clear", "step", t, c, step); });
	}
	clipDone();
}

// A pad in Pattern mode: its slot, as it plays.
function clipPattern(slot) {
	if (armed === "copy") {
		clip = { kind: "pattern", source: slot };
		clipOp("pattern " + (slot + 1) + " copied", function () { stepper.clip("copy", "pattern", slot); }, false);
	} else if (armed === "paste" && clip.kind === "track") {
		// Copied tracks into the slot, on their own numbers; its other tracks stay.
		var mask = 0;
		clip.tracks.forEach(function (s) { mask |= 1 << s; });
		clipOp("tracks " + listOf(clip.tracks) + " pasted into pattern " + (slot + 1), function () {
			stepper.clipPasteTracksToSlot(slot, mask);
			trackReplaced(editTrack);
		});
	} else if (armed === "paste") {
		if (clip.kind !== "pattern") return;
		clipOp("pattern " + (clip.source + 1) + " pasted on " + (slot + 1), function () { stepper.clip("paste", "pattern", slot); trackReplaced(editTrack); });
	} else
		clipOp("pattern " + (slot + 1) + " cleared", function () { stepper.clearPattern(slot); trackReplaced(editTrack); });
	clipDone();
}

// A cycle (the CYCLE page's cells; the selection stays): its overrides, step edits, locks,
// mute. A parameter pasted on one overrides it there. Pasting past the last adds cycles.
function clipCycle(c) {
	var t = editTrack;
	if (armed === "copy") {
		if (c >= stepper.cycleCount(t)) return;
		clip = { kind: "cycle", source: c };
		clipOp("track " + (t + 1) + " cycle " + (c + 1) + " copied", function () { stepper.clip("copy", "cycle", t, c); }, false);
	} else if (armed === "paste" && clip.kind === "cycle") {
		clipOp("cycle " + (clip.source + 1) + " pasted on track " + (t + 1) + " cycle " + (c + 1), function () { stepper.clip("paste", "cycle", t, c); });
	} else if (armed === "paste" && clip.kind === "param") {
		var key = clip.key, v = clip.value;
		clipOp("track " + (t + 1) + " cycle " + (c + 1) + " " + key + " " + v + " pasted", function () { stepper.clipPasteCycleParam(t, c, key, v); });
	} else if (armed === "clear") {
		if (c >= stepper.cycleCount(t)) return;
		clipOp("track " + (t + 1) + " cycle " + (c + 1) + " cleared", function () { stepper.clip("clear", "cycle", t, c); });
	} else return;
	clipDone();
}

// A dial: COPY takes the value it shows (the selected steps' lock, the edited cycle's in
// Cycle edit); CLEAR puts the default back and its random amount to 0 (in Cycle edit the
// cycle's override goes; with steps selected, that lock on them). PASTE: not a target.
function clipDial(key) {
	var t = editTrack, c = cycleEdit ? selCycle : -1, sel = selList();
	var locks = sel.length > 0 && stepper.lockable(key);
	if (armed === "copy") {
		var v = dialValue(t, key);
		if (locks) {
			var locked = stepper.lockGet(t, c, sel[0], key);
			if (locked !== null) v = locked;
		}
		clip = { kind: "param", key: key, value: v };
	} else if (armed === "clear") {
		if (locks) clipOp("track " + (t + 1) + " " + key + " lock cleared on " + sel.length + " steps",
			function () { sel.forEach(function (pos) { stepper.lockClear(t, c, pos, key); }); });
		else if (c >= 0) clipOp("track " + (t + 1) + " cycle " + (c + 1) + " " + key + " cleared", function () {
			stepper.cycleClear(t, c, 2, key);
			stepper.cycleClear(t, c, 7, key);
		});
		else clipOp("track " + (t + 1) + " " + key + " cleared", function () {
			stepper.setParamByKey(t, key, stepper.paramDefault(key));
			if (stepper.getRandom(t, "amount", key)) stepper.setRandom(t, "amount", key, 0);
		});
	} else return;
	clipDone();
}

// A left click somewhere on the device (from [mousestate]): on a dial while armed, the dial
// is the target.
function leftClick(x, y) {
	if (!BRAIN || !stepper || !armed) return;
	x = Number(x);
	y = Number(y);
	for (var i = 0; i < dialArea.length; i++) {
		var d = dialArea[i];
		if (x >= d.x && x < d.x + d.w && y >= d.y && y < d.y + d.h) return clipDial(d.key);
	}
}

// A dial dragged while armed (or just after its click) doesn't turn, as on Rack.
function clipHolds() {
	return armed !== null || (clipClickAt !== null && Date.now() - clipClickAt < 400);
}

// ---- the step pads (brain) -----------------------------------------------------------

// A pad: "pad i v" (v: 1 pressed). In Step mode it turns step (page * 16 + i) of the edit
// track off or on, taken on the next tick like a dial.
function pad(i, v) {
	// A press: "pad i 1", or "pad i bang" (a button-mode live.text bangs); a release, "pad i
	// 0", does nothing.
	if (!BRAIN || !stepper) return;
	if (v !== undefined && String(v) !== "bang" && Number(v) === 0) return;
	var shift = mods.shift, ctrl = mods.ctrl, alt = mods.alt;
	mods = { shift: false, ctrl: false, alt: false };
	i = Math.round(i);
	if (i < 0 || i >= 16) return;
	if (armed) {
		// COPY / PASTE / CLEAR: the pad is the target (a track in Mute mode).
		clipCtrl = ctrl;
		if (padMode === MODE_MUTE) clipTrack(i);
		else if (padMode === MODE_PATTERN) clipPattern(i);
		else clipStep(i);
		return;
	}
	if (alt && padMode === MODE_STEP && isSeq()) return restPad(i); // Alt+click: a rest
	if (padMode === MODE_MUTE) return mutePad(i, ctrl);
	if (padMode === MODE_PATTERN && currentView() === "SONG") return songAdd(i); // SONG: adds the pattern
	if (padMode === MODE_PATTERN) return patternPad(i, shift, ctrl);
	if (ctrl) {
		// A plain-clicked SEQ step joins the lock selection it starts.
		if (!selCount() && seqSel >= 0) lockSel[seqSel] = true;
		seqSel = -1;
		return selectStep(stepPage * 16 + i);
	}
	if (isSeq()) return selectSeqStep(stepPage * 16 + i);
	var t = editTrack, step = stepPage * 16 + i, c = cycleEdit ? selCycle : -1;
	log("pad " + (Math.round(i) + 1) + " pressed (" + v + "): track " + (t + 1) + " step " + (step + 1) + " of " + stepsOf(t));
	if (step < 0 || step >= stepsOf(t)) return;
	ops.push({ desc: "track " + (t + 1) + (c >= 0 ? " cycle " + (c + 1) : "") + " step " + (step + 1) + " toggled", store: true,
		fn: function () { if (c >= 0) stepper.toggleCycleStep(t, c, step); else stepper.toggleStep(t, step); } });
	if (lastStep < 0) takeEdits(-1);
}

// The modifier keys, from [modifiers] just before each pad press: "padMods shift ctrl"
// (Ctrl on Windows, Cmd on the Mac).
// ---- Temp (Rack's): edits made while it's on are temporary ---------------------------
//
// The TEMP button (a parameter: MIDI-mappable, automatable): on, every pattern edit is
// temporary; off reverts them, Ctrl+click off keeps them. Mutes are never reverted; mutes
// staged meanwhile apply as it ends. A pattern switch ends it (reverting), as do Live's
// undo and a paste onto the playing slot; PATTERN's COPY TO (TEMP TO while on) onto the
// playing slot keeps the changes. The pattern stored with the set never has them.
//
// DEVICE's TEMP OFF (0 now, 1 beat, 2 bar, 3 the pattern's loop; default bar): while
// playing, a revert waits for that boundary (TEMP's frame blinks); a click meanwhile
// cancels it (Temp stays on), Ctrl+click keeps the changes at once. Keeping, and turning
// Temp on, are at once (nothing heard changes). Stopping reverts at once.
var tempPending = null; // the state clicked, until the engine takes it (the next 16th)
var tempShown = null, tempWaitShown = null;
var tempReadyAt = 0; // TEMP is a stored parameter: its value coming back as the set loads is ignored
var tempTiming = 2;
var tempEndAt = -1, tempEndSeen = -1; // a revert waiting: the step it's heard from; the last step seen
function tempSelect(v) {
	if (!BRAIN || !stepper) return;
	var ctrl = mods.ctrl;
	mods = { shift: false, ctrl: false, alt: false };
	if (Date.now() < tempReadyAt) { tempShown = null; return showTemp(); }
	var on = Number(v) !== 0;
	if (tempEndAt >= 0) {
		// A revert waiting: Ctrl+click keeps the changes now, a click cancels it.
		tempEndAt = -1;
		tempShown = null;
		if (ctrl) endTemp(true);
		if (lastStep < 0) takeEdits(-1);
		return showTemp();
	}
	if (on === (tempPending !== null ? tempPending : stepper.temp())) return showTemp();
	if (on) {
		tempPending = true;
		ops.push({ desc: "Temp on", store: false, fn: function () { stepper.temp(1); tempPending = null; } });
	} else if (!ctrl && lastStep >= 0 && tempTiming !== 0) {
		// The edits are taken on a step and heard from the next: the first boundary the
		// revert can still make is after the next step.
		tempEndAt = Math.round(stepper.nextBoundary(tempTiming, (lastStep + 1) / 4 + 0.125) * 4);
		tempEndSeen = lastStep;
		log("Temp off: the revert waits for step " + tempEndAt);
		tempShown = null;
		return showTemp();
	} else endTemp(ctrl);
	if (lastStep < 0) takeEdits(-1);
	else showTemp();
}

// Ends Temp, reverting or keeping its changes (taken on the next 16th, or now if stopped,
// by the caller). Mutes staged during Temp apply as it ends (as on Rack).
function endTemp(keep) {
	tempPending = false;
	ops.push({ desc: "Temp off, its changes " + (keep ? "kept" : "reverted"), store: keep, fn: function () {
		stepper.temp(keep ? 3 : 2);
		tempPending = null;
		tempEnded();
	} });
	if (staged) applyMutes();
}

// Each step (brain, before the edits are taken): a revert waiting goes in on the step
// before its boundary, so it's heard from the boundary. After a jump or a loop wrap the
// boundary is found again from where it plays now.
function tempTick(s, jump) {
	if (tempEndAt < 0) return;
	if (!stepper.temp()) { tempEndAt = -1; tempShown = null; return showTemp(); } // a switch ended it
	if (jump || s < tempEndSeen) tempEndAt = Math.round(stepper.nextBoundary(tempTiming, s / 4 + 0.125) * 4);
	tempEndSeen = s;
	if (s + 1 >= tempEndAt) {
		tempEndAt = -1;
		endTemp(false);
	}
}

// After Temp's changes were reverted (or kept): what the selections point at may be gone.
function tempEnded() {
	selCycle = Math.min(selCycle, stepper.cycleCount(editTrack) - 1);
	if (seqSel >= stepsOf(editTrack)) seqSel = -1;
}

// The TEMP button lit while Temp is on ("temp 0/1" from outlet 5: sets it, no output);
// unlit with its frame blinking while a revert waits.
function showTemp() {
	if (!BRAIN || !stepper) return;
	var waiting = tempEndAt >= 0, on = !waiting && (tempPending !== null ? tempPending : stepper.temp());
	var frame = waiting && slowBlinkOn();
	if (tempWaitShown !== frame) {
		tempWaitShown = frame;
		outlet(6, "script", frame ? "show" : "hide", "d_tempwait");
	}
	if (tempShown === on) return;
	tempShown = on;
	outlet(5, "temp", on ? 1 : 0);
	if (DISPLAY && currentView() === "PATTERN") showDisplay(); // COPY TO / TEMP TO
}

function padMods(shift, ctrl, alt) {
	mods = { shift: Number(shift) !== 0, ctrl: Number(ctrl) !== 0, alt: Number(alt) !== 0 };
}

// Where the step pads are on the panel (from the patch: "padGeometry x y cell height"), for
// a right-click.
var padArea = null;
function padGeometry(x, y, cell, h) {
	padArea = { x: Number(x), y: Number(y), cell: Number(cell), h: Number(h) };
}

// A right-click somewhere on the device (from [mousestate]): on a step pad in SEQ mode, the
// step becomes a rest, as on Rack.
function rightClick(x, y) {
	if (!BRAIN || !stepper || !padArea || padMode !== MODE_STEP) return;
	x = Number(x);
	y = Number(y);
	if (y < padArea.y || y >= padArea.y + padArea.h) return;
	var i = Math.floor((x - padArea.x) / padArea.cell);
	if (i < 0 || i >= 16) return;
	restPad(i);
}

// SEQ mode: pad i's step becomes a rest (right-click, or Alt+click).
function restPad(i) {
	if (!isSeq()) return;
	var t = editTrack, c = cycleEdit ? selCycle : -1, step = stepPage * 16 + i;
	if (step >= stepsOf(t)) return;
	var pos = stepper.unrotated(t, c, step);
	if (!stepper.seqStep(t, c, pos)) return;
	ops.push({ desc: "track " + (t + 1) + " SEQ step " + (step + 1) + " a rest", store: true,
		fn: function () { stepper.seqClear(t, c, pos); } });
	if (lastStep < 0) takeEdits(-1);
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
		setStaged(0);
	}
	if (i === MODE_MUTE) preMuteMode = padMode;
	if (i !== MODE_STEP) {
		clearLockSel();
		seqSel = -1;
	}
	padMode = i;
	log("pad mode " + MODE_NAMES[i]);
	showView();
	showPads();
}

// Mute mode: a click stages a change to the track (Apply commits them); Ctrl+click mutes or
// unmutes it at once.
function mutePad(t, ctrl) {
	if (!ctrl) {
		setStaged(staged ^ (1 << t));
		log("track " + (t + 1) + (staged & (1 << t) ? " staged" : " unstaged"));
		showPads();
		return;
	}
	setStaged(staged & ~(1 << t));
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
	setStaged(0);
	ops.push({ desc: "mute changes committed (tracks " + bitList(toggle) + ")", store: true, fn: function () {
		if (lastStep < 0) {
			var m = stepper.perf().muted;
			for (var t = 0; t < TRACKS; t++) if (toggle & (1 << t)) stepper.setMuted(t, !(m & (1 << t)), 0);
		} else stepper.commitMutes(toggle, changeTime(false));
	} });
	if (lastStep < 0) takeEdits(-1);
	else showPads();
}

// The staged mutes, and the "A" (Apply) button: shown only while there are some.
var applyShown = false;
function setStaged(bits) {
	staged = bits;
	if (applyShown === (staged !== 0)) return;
	applyShown = staged !== 0;
	outlet(5, "apply", applyShown ? 1 : 0);
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

// The song: its entries, and where it is as playing now (the engine is ahead of it).
function songShown() {
	var s = stepper.song();
	if (lastStep >= 0 && playing && playing.song) {
		var n = playing.song;
		s.playing = n.playing; s.start = n.start; s.overridden = n.overridden; s.pending = n.pending; s.over = n.over;
	}
	return s;
}

// A pattern switch heard now: the dials and step pads follow the new pattern, and it's
// stored with the set (as any edit).
function checkPattern(perf) {
	if (perf.current === shownPattern) return;
	var first = shownPattern < 0;
	shownPattern = perf.current;
	if (first) return;
	log("pattern " + (perf.current + 1) + " playing");
	selCycle = Math.min(selCycle, stepper.cycleCount(editTrack) - 1);
	lockSel = {};
	seqSel = -1;
	showEdits();
	queueSave();
}

// The [metro] for blinking: the staged and queued pads.
function blink() {
	if (!BRAIN) return;
	blinks++;
	if (padMode !== MODE_STEP || (rec && lastStep < 0)) showPads(); // staged mutes, a queue, the cursor
	if (tempEndAt >= 0) showTemp(); // the frame of a revert waiting
	if (DISPLAY && stepper && (selCount() || cycleEdit || dispShown.stepmark || dispShown.cycmark)) showRandomMark(); // the tab frames
}

function pageCount() {
	return stepper ? Math.max(1, Math.ceil(stepsOf(editTrack) / 16)) : 1;
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
	sel: [0.08, 0.2, 0.5],        // selected for locks (a step with locks has a bar instead)
	selHit: [0.3, 0.6, 1],
	cursor: [1, 0.12, 0.08],      // step recording's cursor (blinking)
	seq: [0.05, 0.3, 0.1],        // SEQ mode: the step selected for KEYS
	seqHit: [0.25, 0.95, 0.4],
};

// The bar along the bottom of a pad whose step has locks (Step mode only).
var padLocks = [];
function setPadLock(i, on) {
	on = !!on;
	if (padLocks[i] === on) return;
	padLocks[i] = on;
	outlet(6, "script", on ? "show" : "hide", "d_padlock" + i);
}

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
	showTemp();
	showDisplay();
	showTrackButtons();
	if (padMode !== MODE_STEP) for (var b = 0; b < 16; b++) setPadLock(b, false);
	if (padMode === MODE_MUTE) return showMutePads(perf);
	if (padMode === MODE_PATTERN) return showPatternPads(perf);
	var t = editTrack, n = stepsOf(t), played = playedOf(t);
	var hits = hitsOf(t), now = playingStep();
	var pages = pageCount();
	if (follow && now >= 0 && Math.floor(now / 16) !== stepPage) {
		stepPage = Math.floor(now / 16);
		outlet(5, "page", stepPage);
	}
	if (stepPage >= pages) {
		stepPage = pages - 1;
		outlet(5, "page", stepPage);
	}
	var lc = cycleEdit ? selCycle : -1, seq = isSeq();
	for (var i = 0; i < 16; i++) {
		var step = stepPage * 16 + i, hit = step < n && hits[step], key, pos = step < n ? stepper.unrotated(t, lc, step) : -1;
		if (step >= n) key = "none";
		else if (step >= played) key = hit ? "cutHit" : "cut";
		else if (step === now) key = hit ? "playHit" : "play";
		else if (rec && lastStep < 0 && seq && pos === stepper.recCursor()) key = slowBlinkOn() ? "cursor" : hit ? "hit" : "off";
		else if (lockSel[pos]) key = hit ? "selHit" : "sel";
		else if (pos === seqSel && seq) key = hit ? "seqHit" : "seq";
		else key = hit ? "hit" : "off";
		setPad(i, PAD_COLOURS[key]);
		setPadLock(i, step < n && stepper.locksAt(t, lc, pos));
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
	var sel = selList(), c = cycleEdit ? selCycle : -1, seq = isSeq();
	for (var i = 0; i < EDIT_KEYS.length; i++) {
		var key = EDIT_KEYS[i], v = dialValue(editTrack, key), lockable = stepper.lockable(key);
		if (sel.length && lockable) {
			var locked = stepper.lockGet(editTrack, c, sel[0], key);
			if (locked !== null) v = locked;
		}
		outlet(3, key, v);
		outlet(3, "a_" + key, (sel.length && !lockable) || (seq && SEQ_IGNORES.indexOf(key) >= 0) ? 0 : 1);
	}
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
		if (tk[0] === "L") {
			var lt = Number(tk[1]), lc = Number(tk[3]), sel = selList();
			sel.forEach(function (pos) { stepper.lockSet(lt, lc, pos, tk[2], v); });
			done.push("track " + (lt + 1) + " " + tk[2] + " " + v + " locked on " + sel.length + " steps");
			continue;
		}
		if (tk.length > 2) stepper.setCycleParam(Number(tk[0]), Number(tk[2]), tk[1], v);
		else stepper.setParamByKey(Number(tk[0]), tk[1], v);
		done.push("track " + (Number(tk[0]) + 1) + (tk.length > 2 ? " cycle " + (Number(tk[2]) + 1) : "") + " " + tk[1] + " " + v);
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
	if (s >= 0 && playing) {
		playing.perf = stepper.perf();
		playing.song = stepper.songNow();
	}
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
	// The edit track and its group go along (read back when the set loads), not stored on
	// their own: a track click isn't an undo step.
	var text = stepper.saveBank().replace(/^\{/, '{"ui":{"editTrack":' + editTrack + ',"group":' + group + ',"tempTiming":' + tempTiming + '},');
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

// The edit track and group saved with the bank, as the set loads.
function restoreUi(text) {
	try {
		var ui = JSON.parse(text).ui;
		if (!ui) return;
		editTrack = Math.max(0, Math.min(TRACKS - 1, Math.round(ui.editTrack) || 0));
		group = (Math.round(ui.group) || 0) & 0xffff & ~(1 << editTrack);
		if (ui.tempTiming !== undefined) tempTiming = Math.max(0, Math.min(3, Math.round(ui.tempTiming) || 0));
	} catch (e) {}
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
	// Find this channel's entries.
	var i = 3, entries = [];
	for (var c = 0; c < 16 && i < arguments.length; c++) {
		var n = arguments[i++];
		if (c === channel) entries = Array.prototype.slice.call(arguments, i, i + n * ENTRY);
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

// Monitoring: "mon id channel pitch velocity" from the brain, the notes played into it while
// the edit track is in SEQ mode: played at once on the receivers of that track's channel.
function mon(id, ch, pitch, velocity) {
	if (BRAIN || !mine(id) || Number(ch) !== channel) return;
	outlet(0, Number(pitch), Number(velocity));
	if (Number(velocity) > 0) used[pitch] = true;
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
		text = id + " ch " + (channel + 1) + " | notes " + stats.notes + " | MISSES " + stats.misses +
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
	["init", "instanceSelect", "channelSelect", "lookahead", "tick", "hello", "ev", "cue", "mon", "noteTime", "noteIn", "recSelect", "ovdSelect",
		"loopinfo", "look", "announce", "anything", "status", "reset", "param",
		"editTrackSelect", "debug", "savepattern", "restore", "enable", "pad", "stepPageSelect", "followSelect", "padMods", "padModeSelect",
		"applyMutes", "blink", "displayPage", "disp", "phraseGrid", "poolKey", "scaleKey", "cycleEditSelect", "lane", "laneClear", "lockStepClick", "cycleLaneClick", "padGeometry", "rightClick",
		"clipSelect", "leftClick", "dialGeometry", "trackButton", "tempSelect", "gearSelect"].forEach(function (n) {
		if (typeof g[n] !== "function") return;
		var f = guarded(n, g[n]);
		if (g[n].immediate) f.immediate = g[n].immediate; // keep scheduler-thread handlers there
		g[n] = f;
	});
})();
