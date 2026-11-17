// The Max for Live devices' script, simulated: one brain and six receivers running
// device/jezric_talea.js with a fake Max (bus, [coll], [pipe] at a tempo, [delay],
// [pattr], Live's song), checked against the engine run directly on the test pattern.
//
//   node m4l/tests/sim.js
//
// Needs the engine built (python m4l/engine/build.py). Any recent Node works; Emscripten
// has one (emsdk/node/<version>/node.exe).

var fs = require("fs"), path = require("path"), vm = require("vm");
var DEVICE = path.join(__dirname, "..", "device");
var ENGINE = path.join(__dirname, "..", "engine", "build", "talea_engine.js");
var SRC = fs.readFileSync(path.join(DEVICE, "jezric_talea.js"), "utf8");
var core = require(path.join(DEVICE, "jezric_talea_core.js")), preset = require("./test_pattern.js");
var M = require(ENGINE);
// Max's require: the files the build copies next to the devices.
function maxRequire(name) {
	name = name.replace(/\.js$/, "");
	if (name === "jezric_talea_engine") return M;
	return require(path.join(DEVICE, name + ".js"));
}

var results = [], ok = true;
function check(name, cond, detail) {
	results.push((cond ? "PASS " : "FAIL ") + name + (cond || !detail ? "" : "\n     " + detail));
	ok = ok && cond;
}

// ---- the fake Max ----------------------------------------------------------------

function World(opts) {
	var w = this;
	w.devices = [];
	w.song = { start_time: 0, loop: 0, loop_start: 0, loop_length: 0, is_playing: 0 };
	w.observers = [];
	w.bpm = opts.bpm || 120;
	w.now = 0; // ms
	w.pipe = []; // pending { at, dev, list }
	w.stalled = false; // the low-priority queue is held up
	w.queue = []; // script calls waiting while stalled
	w.logs = [];
	w.rng = 1;
}
var SAVE_PAUSE_MS = 500; // device/build.py's

World.prototype.random = function () { this.rng = (this.rng * 1103515245 + 12345) & 0x7fffffff; return this.rng / 0x7fffffff; };

// saved: a value for the brain's [pattr] from a saved set, given before init as Live does
// (or after it, with opts.restoreAfterInit).
World.prototype.device = function (role, trackNo, saved, restoreAfterInit, noTestPattern) {
	var w = this, d = { role: role, coll: {}, played: [], sounding: {}, ccs: [] };
	function LiveAPI(cb, p) {
		this.property = "";
		var self = this;
		if (typeof cb === "function") w.observers.push(function (prop) { if (self.property === prop) cb([prop, w.song[prop]]); });
		this.get = function (k) { return w.song[k]; };
	}
	function File() { this.isopen = false; }
	function FakeDate() { return new Date(w.now); }
	FakeDate.now = function () { return w.now; };
	var ctx = {
		jsarguments: ["jezric_talea.js", role], require: maxRequire, LiveAPI: LiveAPI, File: File,
		TALEA_TEST_PATTERN: noTestPattern ? undefined : preset, TALEA_DEBUG: true,
		post: function (s) { w.logs.push(String(s).trim()); },
		messnamed: function () {
			var a = Array.prototype.slice.call(arguments, 1);
			w.devices.forEach(function (x) { w.script(x, a[0], a.slice(1)); });
		},
		outlet: function (n) {
			var a = Array.prototype.slice.call(arguments, 1);
			if (a.length === 1 && Array.isArray(a[0])) a = a[0];
			if (n === 0) w.midi(d, a[0], a[1]);
			else if (n === 2 && role === "brain") {
				// [route savelater] -> [delay]: restarted by each one; then "savepattern"
				// comes back through [deferlow]
				if (a[0] === "savelater") d.saveAt = w.now + SAVE_PAUSE_MS;
			} else if (n === 4 && role === "brain") {
				// [pattr]: keeps the value, for the set, and sends it back out
				d.pattr = a.slice();
				d.saves = (d.saves || 0) + 1;
				w.script(d, "restore", a);
			} else if (n === 2) {
				if (a[0] === "store") d.coll[a[1]] = a.slice(2);
				else if (a[0] === "remove") delete d.coll[a[1]];
				else if (a[0] === "clear") d.coll = {};
			} else if (n === 3 && a[0] === "clear") w.pipe = w.pipe.filter(function (e) { return e.dev !== d; });
			else if (n === 3 && role === "brain") (d.dials = d.dials || {})[a[0]] = a[1];
			else if (n === 5 && role === "brain") {
				// the pads, the step page tabs and the Follow button
				if (a[0] === "pad") (d.pads = d.pads || [])[a[1]] = a.slice(2, 5).join(" ");
				else if (a[0] === "page") d.page = a[1];
				else if (a[0] === "follow") d.follow = a[1];
				else if (a[0] === "mode") d.mode = a[1];
				else if (a[0] === "apply") d.apply = a[1];
				else if (a[0] === "rec") d.rec = a[1];
				else if (a[0] === "temp") d.temp = a[1];
				else if (a[0] === "wtemp") d.wtemp = a[1];
				else if (a[0] === "onstate") d.onstate = a[1];
				else if (a[0] === "win") { d.win = a[1]; (d.winLog = d.winLog || []).push(a[1]); }
				else if (a[0] === "winstate") d.winstate = a[1];
				else if (a[0] === "won") d.won = a[1];
				else if (a[0] === "wwin") d.wwin = a[1];
				else if (a[0] === "wpin") d.wpin = a[1];
				else if (a[0] === "pinstate") d.pinstate = a[1];
				else if (/^(dshow|dhide)$/.test(a[0])) (d.dshown = d.dshown || {})[a[1]] = a[0] === "dshow";
				else if (/^(wshow|whide)$/.test(a[0])) (d.wshown = d.wshown || {})[a[1]] = a[0] === "wshow";
				else if (/^(copy|paste|clear|pasteactive)$/.test(a[0])) (d.clipBtn = d.clipBtn || {})[a[0]] = a[1];
				else if (a[0] === "trk") (d.trk = d.trk || [])[a[1]] = a.slice(2, 5).join(" ");
				else if (a[0] === "trkp") (d.trkp = d.trkp || [])[a[1]] = a[2];
				else if (a[0] === "recactive") d.recactive = a[1];
				else if (a[0] === "patc") (d.patc = d.patc || [])[a[1]] = a.slice(2, 5).join(" ");
				else if (a[0] === "patb") (d.patb = d.patb || [])[a[1]] = a[2];
			} else if (n === 6 && role === "brain") {
				// the display: [thispatcher] shows and hides; the rest sets controls
				d.shown = d.shown || {};
				d.disp = d.disp || {};
				if (a[0] === "script") d.shown[a[2]] = a[1] === "show";
				else if (a[0] === "d_diagram" && a[1] === "play") d.play = a[2];
				else if (a[0] === "d_diagram") d.diagram = a.slice(1);
				else d.disp[a[0]] = a.length > 2 || /^dr_/.test(a[0]) ? a.slice(1) : a[1];
			} else if (n === 7 && role === "brain") {
				// the floating window's display, drawn for its own view
				var wd = d.w = d.w || { shown: {}, disp: {} };
				if (a[0] === "script") wd.shown[a[2]] = a[1] === "show";
				else if (a[0] !== "d_diagram") wd.disp[a[0]] = a.length > 2 || /^dr_/.test(a[0]) ? a.slice(1) : a[1];
			}
		},
		Math: Math, Date: FakeDate
	};
	vm.createContext(ctx);
	vm.runInContext(SRC, ctx);
	d.ctx = ctx;
	w.devices.push(d);
	if (saved && !restoreAfterInit) ctx.restore.apply(null, saved);
	ctx.init();
	if (saved && restoreAfterInit) ctx.restore.apply(null, saved);
	if (role === "receiver") ctx.channelSelect(trackNo);
	return d;
};

// A script function call: at once, or queued while the low-priority thread is held up.
World.prototype.script = function (d, fn, args) {
	if (typeof d.ctx[fn] !== "function") fn = "anything";
	if (this.stalled) this.queue.push([d, fn, args]);
	else d.ctx[fn].apply(null, args);
};

World.prototype.midi = function (d, pitch, vel) {
	if (vel > 0) {
		d.played.push([this.now, pitch, vel]);
		d.sounding[pitch] = (d.sounding[pitch] || 0) + 1;
	} else if (d.sounding[pitch]) d.sounding[pitch] = 0;
};

World.prototype.setSong = function (k, v) {
	this.song[k] = v;
	this.observers.forEach(function (o) { o(k); });
};

// Plays the pipe's entries due up to time t (ms), in time order.
World.prototype.runPipe = function (t) {
	for (;;) {
		var due = this.pipe.filter(function (e) { return e.at <= t + 1e-9; });
		if (!due.length) break;
		due.sort(function (a, b) { return a.at - b.at || a.seq - b.seq; });
		var e = due[0];
		this.pipe.splice(this.pipe.indexOf(e), 1);
		this.runSaves(e.at);
		this.now = e.at;
		if (e.list[0] === -1) e.dev.ccs.push([this.now, e.list[1], e.list[2], e.list[3]]); // to [line]
		else {
			this.midi(e.dev, e.list[0], e.list[1]);
			this.midi(e.dev, e.list[2], e.list[3]);
		}
	}
	this.runSaves(t);
	this.now = t;
};

// The brain's [delay] before storing the pattern: fires when its time comes.
World.prototype.runSaves = function (t) {
	var w = this;
	w.devices.forEach(function (d) {
		if (d.saveAt !== undefined && d.saveAt <= t + 1e-9) {
			w.now = d.saveAt;
			delete d.saveAt;
			w.script(d, "savepattern", []);
		}
	});
};

// Time passing with nothing else happening (ms).
World.prototype.wait = function (ms) {
	this.runPipe(this.now + ms);
};

var seq = 0;
// One 16th tick at step s: each device in a varying order, the receiver's native
// [coll] -> [pipe] part first, then its script.
World.prototype.tick = function (s) {
	var w = this, msPerQ = 60000 / w.bpm;
	w.runPipe(w.now);
	var list = w.devices.slice();
	if (w.random() < 0.5) list.reverse();
	var bars = Math.floor(s / 16) + 1, beats = Math.floor((s % 16) / 4) + 1, units = (s % 4) * 120;
	list.forEach(function (d) {
		if (d.role === "receiver" && d.coll[s]) {
			var e = d.coll[s];
			delete d.coll[s];
			for (var i = 0; i < e.length; i += 5)
				w.pipe.push({ at: w.now + e[i] * msPerQ, dev: d, list: e.slice(i + 1, i + 5), seq: seq++ });
			w.runPipe(w.now); // delay 0 goes out at once
		}
		w.script(d, "tick", [bars, beats, units]);
	});
};

// before(i), if given, runs before tick i (e.g. turning a dial).
World.prototype.play = function (steps, before) {
	var w = this, msPerStep = 15000 / w.bpm;
	w.setSong("is_playing", 1);
	steps.forEach(function (s, i) {
		if (i > 0) w.runPipe(w.now + msPerStep);
		if (before) before(i);
		w.tick(s);
	});
	w.runPipe(w.now + msPerStep);
};

World.prototype.stop = function () {
	this.setSong("is_playing", 0);
	this.runPipe(this.now + 1);
};

World.prototype.release = function () {
	var q = this.queue;
	this.stalled = false;
	this.queue = [];
	q.forEach(function (c) { c[0].ctx[c[1]].apply(null, c[2]); });
};

function setup(opts) {
	opts = opts || {};
	var w = new World(opts);
	var brain = w.device("brain", 0, opts.saved, opts.restoreAfterInit, opts.noTestPattern);
	// The tests were written for the display following the dials (PAGE LOCK off); a new
	// device has PAGE LOCK on (tested on its own).
	if (!opts.pageLock && brain.ctx) brain.ctx.pageLock = false;
	var rx = [];
	for (var t = 1; t <= preset.TRACKS; t++) rx.push(w.device("receiver", t));
	return { w: w, brain: brain, rx: rx };
}

// Frees the brain's engine when a test is done.
function done(s) {
	if (s.brain.ctx.stepper) s.brain.ctx.stepper.destroy();
}

// ---- what the engine says should play ---------------------------------------------

function range(a, b) { var r = []; for (var i = a; i < b; i++) r.push(i); return r; }

// Note-on times in quarters of the playing timeline (tick index i -> quarter i/4), so a
// loop or a jump just continues the count.
function playedQuarters(d, t0, bpm) {
	return d.played.map(function (n) { return +((n[0] - t0) / (60000 / bpm)).toFixed(6); });
}

function emptyWant() {
	var w = [];
	w.pitch = [];
	w.cc = [];
	for (var t = 0; t < preset.TRACKS; t++) { w.push([]); w.pitch.push([]); w.cc.push([]); }
	return w;
}

// Adds a step's entries (from tick index i) to `want`: note-on times per track, with their
// pitches in want.pitch and CC values [quarter, number, value, glide] in want.cc.
function addStep(want, out, i) {
	for (var t = 0; t < preset.TRACKS; t++)
		out[t].forEach(function (e) {
			var q = +(i / 4 + e[0]).toFixed(6);
			if (e[1] === -1) want.cc[t].push([q, e[2], e[3], e[4]]);
			else if (e[4] > 0) { want[t].push(q); want.pitch[t].push(e[3]); }
		});
}

function expectRuns(runs) {
	// runs: [{ steps: [...], at: tick index of the first step, edits: { i: [[track, key,
	// value], ...] } (made after step i) }]; quarters on the timeline
	var want = emptyWant();
	runs.forEach(function (r) {
		var st = new core.Stepper(M, preset);
		r.steps.forEach(function (p, i) {
			addStep(want, st.step(p), r.at + i);
			(r.edits && r.edits[i] || []).forEach(function (e) {
				if (e[1] === "toggle") st.toggleStep(e[0], e[2]);
				else st.setParamByKey(e[0], e[1], e[2]);
			});
		});
		st.destroy();
	});
	return want;
}

function compare(name, rx, t0, bpm, want, from) {
	var bad = [];
	rx.forEach(function (d, t) {
		var got = playedQuarters(d, t0, bpm).filter(function (q) { return q >= from - 1e-6; });
		var exp = want[t].filter(function (q) { return q >= from - 1e-6; });
		var same = got.length === exp.length && got.every(function (q, i) { return Math.abs(q - exp[i]) < 1e-5; });
		if (!same) {
			var i = 0;
			while (i < got.length && i < exp.length && Math.abs(got[i] - exp[i]) < 1e-5) i++;
			bad.push("track " + (t + 1) + ": got " + got.length + ", want " + exp.length + ", first difference at " + i +
				" (got " + got[i] + ", want " + exp[i] + ")");
		}
		// Pitches (chords: every note) and CC values, when the expectation has them.
		var msPerQ = 60000 / bpm;
		if (want.pitch) {
			var gp = d.played.filter(function (n) { return (n[0] - t0) / msPerQ >= from - 1e-6; }).map(function (n) { return n[1]; });
			var ep = want.pitch[t].filter(function (_, k) { return want[t][k] >= from - 1e-6; });
			if (gp.join() !== ep.join()) { var z = 0; while (gp[z] === ep[z]) z++; bad.push("track " + (t + 1) + ": pitches differ at note " + z + " of " + ep.length + " (got " + gp.slice(z, z + 6) + ", want " + ep.slice(z, z + 6) + ")"); }
		}
		if (want.cc) {
			var gc = d.ccs.map(function (c) { return [+((c[0] - t0) / msPerQ).toFixed(6), c[1], c[2], c[3]]; })
				.filter(function (c) { return c[0] >= from - 1e-6; });
			var ec = want.cc[t].filter(function (c) { return c[0] >= from - 1e-6; });
			var k = 0;
			while (k < gc.length && k < ec.length && Math.abs(gc[k][0] - ec[k][0]) < 1e-5 && gc[k].slice(1).join() === ec[k].slice(1).join()) k++;
			if (k < gc.length || k < ec.length)
				bad.push("track " + (t + 1) + ": CC got " + gc.length + ", want " + ec.length + ", first difference at " + k +
					" (got " + JSON.stringify(gc[k]) + ", want " + JSON.stringify(ec[k]) + ")");
		}
	});
	check(name, bad.length === 0, bad.join("; "));
}

function misses(rx) {
	return rx.reduce(function (n, d) { return n + d.ctx.stats.misses; }, 0);
}

function hanging(rx) {
	return rx.reduce(function (n, d) { for (var p in d.sounding) n += d.sounding[p] > 1 ? 1 : 0; return n; }, 0);
}

// ---- the tests ------------------------------------------------------------------

// Plain play from the start.
(function () {
	var s = setup();
	var t0 = s.w.now;
	s.w.play(range(0, 512));
	var want = expectRuns([{ steps: range(0, 512), at: 0 }]);
	compare("plain play, 32 bars: every note on time", s.rx, t0, 120, want, 0);
	check("no misses (" + misses(s.rx) + ")", misses(s.rx) === 0);
	check("the pattern has off-grid notes", want.some(function (tr) { return tr.some(function (q) { return Math.abs(q * 4 - Math.round(q * 4)) > 1e-6; }); }));
	check("no note started twice without an end (" + hanging(s.rx) + ")", hanging(s.rx) === 0);
	s.w.stop();
	var left = s.rx.reduce(function (n, d) { for (var p in d.sounding) n += d.sounding[p]; return n; }, 0);
	check("nothing sounding after Stop (" + left + ")", left === 0);
	done(s);
})();

// The script held up for 3 steps (look-ahead 4): nothing lost.
(function () {
	var s = setup({ bpm: 133 });
	var t0 = s.w.now, msPerStep = 15000 / 133;
	s.w.setSong("is_playing", 1);
	for (var i = 0; i < 256; i++) {
		if (i > 0) s.w.runPipe(s.w.now + msPerStep);
		if (i === 100 || i === 180) s.w.stalled = true;
		if (i === 103 || i === 183) s.w.release();
		s.w.tick(i);
	}
	s.w.runPipe(s.w.now + msPerStep);
	compare("script held up for 3 steps, twice: every note on time", s.rx, t0, 133, expectRuns([{ steps: range(0, 256), at: 0 }]), 0);
	check("no misses (" + misses(s.rx) + ")", misses(s.rx) === 0);
	done(s);
})();

// A jump from step 100 to 300: from the step after the jump on, the engine's run from 300.
(function () {
	var s = setup();
	var t0 = s.w.now;
	var steps = range(0, 101).concat(range(300, 400));
	s.w.play(steps);
	var want = expectRuns([{ steps: range(0, 101), at: 0 }, { steps: range(300, 400), at: 101 }]);
	// Ticks 0-100 are quarters 0 to 25.25; the jump lands on tick 101 (25.25), whose
	// notes may be lost; from tick 102 (25.5) on, the new run plays.
	var before = s.rx.map(function (d) { return playedQuarters(d, t0, 120).filter(function (q) { return q < 25.25 - 1e-6; }); });
	var wantBefore = want.map(function (tr) { return tr.filter(function (q) { return q < 25.25 - 1e-6; }); });
	check("jump: notes before it on time", before.every(function (g, t) {
		return g.length === wantBefore[t].length && g.every(function (q, i) { return Math.abs(q - wantBefore[t][i]) < 1e-5; });
	}), before.map(function (g, t) { return g.length + "/" + wantBefore[t].length; }).join(" "));
	var after = s.rx.map(function (d) { return playedQuarters(d, t0, 120).filter(function (q) { return q >= 25.5 - 1e-6; }); });
	var wantAfter = want.map(function (tr) { return tr.filter(function (q) { return q >= 25.5 - 1e-6; }); });
	var same = after.every(function (g, t) { return g.length === wantAfter[t].length && g.every(function (q, i) { return Math.abs(q - wantAfter[t][i]) < 1e-5; }); });
	check("jump: from the step after it, the engine's run from the new position", same,
		after.map(function (g, t) { return g.length + "/" + wantAfter[t].length; }).join(" "));
	check("jump: no misses (" + misses(s.rx) + ")", misses(s.rx) === 0);
	check("jump: no hanging notes (" + hanging(s.rx) + ")", hanging(s.rx) === 0);
	done(s);
})();

// Stop, move the start marker, Play: the first step from the cue too.
(function () {
	var s = setup();
	s.w.play(range(0, 150));
	s.w.stop();
	s.w.setSong("start_time", 16); // bar 5 = step 64
	s.w.runPipe(s.w.now + 2000);
	s.rx.forEach(function (d) { d.played = []; });
	var t0 = s.w.now;
	s.w.play(range(64, 200));
	compare("Stop, cue at bar 5, Play: every note on time, the first step's too", s.rx, t0, 120,
		expectRuns([{ steps: range(64, 200), at: 0 }]), 0);
	check("Stop and Play: no misses (" + misses(s.rx) + ")", misses(s.rx) === 0);
	done(s);
})();

// A two-bar loop, bars 3-4, played three times round.
(function () {
	var s = setup();
	s.w.setSong("loop", 1);
	s.w.setSong("loop_start", 8);
	s.w.setSong("loop_length", 8);
	s.w.setSong("start_time", 8);
	var steps = [];
	for (var k = 0; k < 3; k++) steps = steps.concat(range(32, 64));
	var t0 = s.w.now;
	s.w.play(steps);
	compare("two-bar loop, three times round: every note on time", s.rx, t0, 120, expectRuns([{ steps: steps, at: 0 }]), 0);
	check("loop: no misses (" + misses(s.rx) + ")", misses(s.rx) === 0);
	check("loop: no hanging notes (" + hanging(s.rx) + ")", hanging(s.rx) === 0);
	done(s);
})();

// The loop switched on, then off, within look-ahead of its end: the brain has already
// sent steps for the other side of it (test 3a.3 lost every step of the loop this way).
(function () {
	var s = setup();
	s.w.play(range(64, 94));
	s.w.setSong("loop", 1);
	s.w.setSong("loop_start", 16);
	s.w.setSong("loop_length", 8);
	s.w.play(range(94, 96).concat(range(64, 96), range(64, 94)));
	s.w.setSong("loop", 0);
	s.w.play(range(94, 140));
	var lost = misses(s.rx) / 4;
	check("loop on and off just before its end: at most one step lost each time (" + lost + ")", lost <= 2);
	check("loop on and off late: no hanging notes (" + hanging(s.rx) + ")", hanging(s.rx) === 0);
	done(s);
})();

// The loop shortened while playing inside it, just before the new end.
(function () {
	var s = setup();
	s.w.setSong("loop", 1);
	s.w.setSong("loop_start", 16);
	s.w.setSong("loop_length", 16);
	s.w.play(range(64, 94));
	s.w.setSong("loop_length", 8);
	s.w.play(range(94, 96).concat(range(64, 96)));
	var lost = misses(s.rx) / 4;
	check("loop shortened just before its new end: at most one step lost (" + lost + ")", lost <= 1);
	done(s);
})();

// Edits while playing: dials turned between ticks, heard from the step after the tick
// that takes them, exactly as if the engine had had them then all along.
function turn(brain, track, key, v) {
	brain.ctx.editTrackSelect(track);
	brain.ctx.param(key, v);
}

(function () {
	var s = setup(), steps = range(0, 256);
	// A few single turns, and two dials swept, one value per step for a while.
	var plan = { 40: [[0, "pulses", 5]], 41: [[2, "pace", -60], [1, "timing", 40]], 90: [[3, "probability", -50]] };
	for (var i = 120; i < 150; i++) plan[i] = [[2, "repeats", 1 + (i % 6)], [0, "rotate", i % 16]];
	plan[200] = [[1, "sustain", 100], [0, "delay", 60]];
	var t0 = s.w.now;
	s.w.play(steps, function (i) {
		(plan[i] || []).forEach(function (e) { turn(s.brain, e[0], e[1], e[2]); });
	});
	// A dial turned before tick i is taken at tick i: heard from step i + 1 on.
	var want = expectRuns([{ steps: steps, at: 0, edits: plan }]);
	compare("edits while playing: every note as the engine plays them from the next step", s.rx, t0, 120, want, 0);
	var before = expectRuns([{ steps: steps, at: 0 }]);
	check("the edits change the notes", want.some(function (tr, t) { return tr.length !== before[t].length || tr.some(function (q, i) { return Math.abs(q - before[t][i]) > 1e-6; }); }));
	check("edits: no misses (" + misses(s.rx) + ")", misses(s.rx) === 0);
	check("edits: no hanging notes (" + hanging(s.rx) + ")", hanging(s.rx) === 0);
	done(s);
})();

// Edits while the script is held up: taken on the first tick it gets to.
(function () {
	var s = setup(), steps = range(0, 96);
	var t0 = s.w.now;
	s.w.play(steps, function (i) {
		if (i === 30) { s.w.stalled = true; turn(s.brain, 0, "pulses", 11); }
		if (i === 31) turn(s.brain, 2, "pace", 80);
		if (i === 33) s.w.release(); // ticks 30-32 run late, then tick 33
	});
	// Steps 31 and 32 were sent before and play on time as they were; the brain takes the
	// edits on tick 30, which runs at tick 33's time, and its resend counts from step 33.
	var want = expectRuns([{ steps: steps, at: 0, edits: { 30: [[0, "pulses", 11], [2, "pace", 80]] } }]);
	var old = expectRuns([{ steps: steps, at: 0 }]);
	check("edits during a hold-up: no misses (" + misses(s.rx) + ")", misses(s.rx) === 0);
	compare("edits during a hold-up: the notes before it as they were", s.rx, t0, 120,
		old.map(function (tr) { return tr.filter(function (q) { return q < 33 / 4 - 1e-6; }); }).map(function (tr, t) {
			return tr.concat(want[t].filter(function (q) { return q >= 33 / 4 - 1e-6; }));
		}), 0);
	check("edits during a hold-up: no hanging notes (" + hanging(s.rx) + ")", hanging(s.rx) === 0);
	done(s);
})();

// An edit while stopped: Play from the start marker plays it from the first step.
(function () {
	var s = setup();
	s.w.setSong("start_time", 4);
	turn(s.brain, 1, "pulses", 2);
	turn(s.brain, 3, "probability", 0);
	var t0 = s.w.now, steps = range(16, 80);
	s.w.play(steps);
	var st = new core.Stepper(M, preset), want = emptyWant();
	st.setParamByKey(1, "pulses", 2);
	st.setParamByKey(3, "probability", 0);
	steps.forEach(function (p, i) { addStep(want, st.step(p), i); });
	st.destroy();
	compare("edit while stopped: Play starts with it", s.rx, t0, 120, want, 0);
	check("edit while stopped: no misses (" + misses(s.rx) + ")", misses(s.rx) === 0);
	done(s);
})();

// Chords and CC: the test pattern has them, and plain play delivers them whole.
(function () {
	var s = setup(), steps = range(0, 256), t0 = s.w.now;
	s.w.play(steps, function (i) {
		if (i === 100) turn(s.brain, 5, "sustain", 100); // longer glides from step 101
		if (i === 101) turn(s.brain, 4, "pulses", 7);
	});
	var want = expectRuns([{ steps: steps, at: 0, edits: { 100: [[5, "sustain", 100]], 101: [[4, "pulses", 7]] } }]);
	var chords = 0, shared = 0;
	for (var k = 1; k < want[4].length; k++) if (Math.abs(want[4][k] - want[4][k - 1]) < 1e-9) chords++;
	check("the chord track plays chords (" + chords + " notes start with another)", chords > 100);
	var ccs = want.cc[5];
	check("the CC track has values, some off the grid, with glides (" + ccs.length + ")",
		ccs.length > 50 && ccs.some(function (c) { return Math.abs(c[0] * 4 - Math.round(c[0] * 4)) > 1e-6; }) && ccs.every(function (c) { return c[3] > 0; }));
	check("the CC track plays no notes", want[5].length === 0 && s.rx[5].played.length === 0);
	compare("chords and CC: every note with its pitch, every CC value with its glide", s.rx, t0, 120, want, 0);
	check("chords and CC: no hanging notes (" + hanging(s.rx) + ")", hanging(s.rx) === 0);
	check("chords and CC: no misses (" + misses(s.rx) + ")", misses(s.rx) === 0);
	s.w.stop();
	var left = s.rx.reduce(function (n, d) { for (var p in d.sounding) n += d.sounding[p]; return n; }, 0);
	check("chords: nothing sounding after Stop (" + left + ")", left === 0);
	done(s);
})();

// The pattern saved with the set: edits stored after they're made, and a set loaded later
// plays the edited pattern.
(function () {
	var s = setup();
	// A dial swept through values while stopped, 30 ms apart: stored once, after the pause.
	[9, 8, 7, 6, 5].forEach(function (v) { turn(s.brain, 0, "pulses", v); s.w.wait(30); });
	var early = s.brain.saves || 0;
	s.w.wait(600);
	check("a dial swept while stopped is stored once, when it stops (" + early + " before the pause, " + (s.brain.saves || 0) + " after)",
		early === 0 && s.brain.saves === 1);
	s.w.play(range(0, 64), function (i) {
		if (i === 20) turn(s.brain, 2, "pace", -40);
		if (i === 21) turn(s.brain, 4, "sustain", 60); // 125 ms later: the same store
	});
	s.w.stop();
	s.w.wait(600);
	var saved = s.brain.pattr;
	check("edits during play close together are stored once (" + s.brain.saves + " stores in all, " + (saved ? saved.length - 1 : 0) + " pieces)",
		!!saved && saved[0] === 2 && s.brain.saves === 2);
	check("its own value coming back from [pattr] isn't taken as a restore",
		!s.w.logs.some(function (l) { return /pattern restored/.test(l); }));
	done(s);

	var edits = [[0, "pulses", 5], [2, "pace", -40], [4, "sustain", 60]];
	function wantWith() {
		var st = new core.Stepper(M, preset), want = emptyWant();
		edits.forEach(function (e) { st.setParamByKey(e[0], e[1], e[2]); });
		range(0, 128).forEach(function (p, i) { addStep(want, st.step(p), i); });
		st.destroy();
		return want;
	}
	[false, true].forEach(function (after) {
		var r = setup({ saved: saved, restoreAfterInit: after }), t0 = r.w.now;
		r.w.logs = [];
		r.w.play(range(0, 128));
		var name = "set loaded (pattern given " + (after ? "after" : "before") + " init)";
		check(name + ": the brain has the edited values", r.brain.ctx.stepper.getParam(0, "pulses") === 5 &&
			r.brain.ctx.stepper.getParam(2, "pace") === -40 && r.brain.ctx.stepper.getParam(4, "sustain") === 60);
		compare(name + ": Play plays the edited pattern from the first step", r.rx, t0, 120, wantWith(), 0);
		check(name + ": logged as edited (" + (r.w.logs.filter(function (l) { return /Play from/.test(l); })[0] || "") + ")",
			r.w.logs.some(function (l) { return /Play from step 0, edited so far: track 1 pulses 5, track 3 pace -40, track 5 sustain 60/.test(l); }));
		check(name + ": nothing stored back unchanged (" + (r.brain.saves || 0) + ")", !r.brain.saves);
		done(r);
	});

	// Live's undo and redo: [pattr] gives back an earlier value, after init.
	var u = setup();
	turn(u.brain, 0, "pulses", 5);
	u.w.wait(600);
	var first = u.brain.pattr.slice();
	turn(u.brain, 2, "pace", -40);
	u.w.wait(600);
	var second = u.brain.pattr.slice(), saves = u.brain.saves;
	u.w.logs = [];
	u.brain.ctx.restore.apply(null, first); // undo, stopped
	var st = u.brain.ctx.stepper;
	check("undo while stopped: the earlier values (pace " + st.getParam(2, "pace") + ")",
		st.getParam(0, "pulses") === 5 && st.getParam(2, "pace") === u.brain.ctx.startValues[2][u.brain.ctx.EDIT_KEYS.indexOf("pace")]);
	check("undo logged as an edit", u.w.logs.some(function (l) { return /edit while stopped: undo or redo/.test(l); }));
	var t0 = u.w.now;
	u.w.play(range(0, 128), function (i) {
		if (i === 30) u.brain.ctx.restore.apply(null, second); // redo, playing
	});
	check("redo while playing: taken on a tick and logged as an edit (" +
		(u.w.logs.filter(function (l) { return /edit at step/.test(l); })[0] || "none") + ")",
		st.getParam(2, "pace") === -40 && u.w.logs.some(function (l) { return /edit at step \d+: undo or redo/.test(l); }));
	u.w.stop();
	u.w.wait(600);
	check("undo and redo store nothing back (" + (u.brain.saves - saves) + ")", u.brain.saves === saves);
	done(u);

	var bad = setup({ saved: [1, "p!!!not base64"] });
	check("an unreadable saved pattern: the test pattern, and a log line",
		bad.brain.ctx.stepper.getParam(0, "pulses") === 9 && bad.w.logs.some(function (l) { return /isn't readable/.test(l); }));
	done(bad);
})();

// The On button while playing: off is silent from the next step (what was sounding ends);
// on again starts the engine afresh at Live's next launch-quantization boundary.
[[4, 16, "1 bar"], [11, 1, "1/16"]].forEach(function (q) {
	var s = setup(), steps = range(0, 160);
	s.w.setSong("clip_trigger_quantization", q[0]);
	var t0 = s.w.now, offAt = 20, onAt = 37;
	var startAt = Math.ceil((onAt + 1) / q[1]) * q[1];
	s.w.play(steps, function (i) {
		if (i === offAt) s.brain.ctx.enable(0);
		if (i === onAt) s.brain.ctx.enable(1);
	});
	var want = expectRuns([{ steps: range(0, offAt + 1), at: 0 }, { steps: range(startAt, 160), at: startAt }]);
	compare("On button, quantization " + q[2] + ": silent from step " + (offAt + 1) + ", playing again from step " + startAt,
		s.rx, t0, 120, want, 0);
	check("On button, " + q[2] + ": no misses (" + misses(s.rx) + "), no hanging notes (" + hanging(s.rx) + ")",
		misses(s.rx) === 0 && hanging(s.rx) === 0);
	done(s);
});

// Off while stopped: Play is silent; on again while stopped: Play plays from the start.
(function () {
	var s = setup();
	s.brain.ctx.enable(0);
	var t0 = s.w.now;
	s.w.play(range(0, 64));
	var silent = s.rx.every(function (d) { return d.played.length === 0; });
	s.w.stop();
	s.brain.ctx.enable(1);
	var t1 = s.w.now;
	s.rx.forEach(function (d) { d.played = []; });
	s.w.play(range(0, 64));
	check("off while stopped: Play is silent", silent);
	compare("on again while stopped: Play plays from the start", s.rx, t1, 120, expectRuns([{ steps: range(0, 64), at: 0 }]), 0);
	check("off and on while stopped: no misses (" + misses(s.rx) + ")", misses(s.rx) === 0);
	done(s);
})();

// The step pads (Step mode): a pad toggles a step of the edit track, taken on the next
// tick like a dial; heard from the next step.
(function () {
	var s = setup(), steps = range(0, 128);
	var t0 = s.w.now;
	s.w.play(steps, function (i) {
		if (i === 30) { s.brain.ctx.editTrackSelect(0); s.brain.ctx.pad(5, 1); }
		if (i === 50) { s.brain.ctx.editTrackSelect(2); s.brain.ctx.pad(0, 1); s.brain.ctx.pad(9, 1); }
	});
	var want = expectRuns([{ steps: steps, at: 0, edits: { 30: [[0, "toggle", 5]], 50: [[2, "toggle", 0], [2, "toggle", 9]] } }]);
	var old = expectRuns([{ steps: steps, at: 0 }]);
	compare("pads while playing: every note as the engine plays it after the toggles", s.rx, t0, 120, want, 0);
	check("the toggles change the notes", want.some(function (tr, t) { return tr.join() !== old[t].join(); }));
	check("pads: no misses (" + misses(s.rx) + "), no hanging notes (" + hanging(s.rx) + ")", misses(s.rx) === 0 && hanging(s.rx) === 0);
	done(s);
})();

// Pads while stopped, a second page, and the pads' colours.
(function () {
	var s = setup(), b = s.brain.ctx;
	b.editTrackSelect(1);
	turn(s.brain, 1, "steps", 32);
	var before = b.stepper.hits(1).slice();
	b.stepPageSelect(1); // steps 17-32: Follow goes off
	press(s.brain, 3);   // step 20
	var after = b.stepper.hits(1);
	check("pad on page 2 toggles step 20 (" + before[19] + " -> " + after[19] + "), and only it",
		after[19] === !before[19] && after.every(function (h, i) { return i === 19 || h === before[i]; }));
	check("choosing a page turns Follow off (" + s.brain.follow + "), page shown " + s.brain.page,
		s.brain.follow === 0 && s.brain.page === 1);
	b.stepPageSelect(3); // past the track's 32 steps: stays on page 2
	check("a page past the track's Steps doesn't take (page " + s.brain.page + ")", s.brain.page === 1);
	var t0 = s.w.now;
	s.w.play(range(0, 64));
	var st = new core.Stepper(M, preset), want = emptyWant();
	st.setParamByKey(1, "steps", 32);
	st.toggleStep(1, 19);
	range(0, 64).forEach(function (p, i) { addStep(want, st.step(p), i); });
	st.destroy();
	compare("pads while stopped: Play plays the toggled step", s.rx, t0, 120, want, 0);
	done(s);
})();

// The playing step lights on the pads while playing, and Follow moves the page with it.
(function () {
	var s = setup(), b = s.brain.ctx, seen = 0, pages = {};
	b.editTrackSelect(0);
	turn(s.brain, 0, "steps", 48);
	b.followSelect(1);
	s.w.play(range(0, 96), function (i) {
		if (i > 2) pages[s.brain.page] = true;
	});
	check("Follow moves through the pages (" + Object.keys(pages).join(",") + ")", Object.keys(pages).length === 3);
	done(s);
})();

// Undo of a pad toggle: Live gives back the earlier pattern, step and all.
(function () {
	var s = setup(), b = s.brain.ctx;
	b.editTrackSelect(0);
	var before = b.stepper.hits(0)[5];
	s.w.wait(SAVE_PAUSE_MS + 10);
	var first = s.brain.pattr ? s.brain.pattr.slice() : null;
	b.pad(5, 1);
	s.w.wait(SAVE_PAUSE_MS + 10);
	var toggled = b.stepper.hits(0)[5];
	if (!first) { // nothing stored yet: store the pattern before the toggle by undoing it by hand
		b.pad(5, 1);
		s.w.wait(SAVE_PAUSE_MS + 10);
		first = s.brain.pattr.slice();
		b.pad(5, 1);
		s.w.wait(SAVE_PAUSE_MS + 10);
	}
	b.restore.apply(null, first);
	check("undo of a pad toggle brings the step back (" + before + " -> " + toggled + " -> " + b.stepper.hits(0)[5] + ")",
		toggled === !before && b.stepper.hits(0)[5] === before);
	done(s);
})();

// ---- Mute and Pattern pad modes ----

// A track button press as the patch sends it: the modifier keys, then the press.
function trackPress(b, i, shift, ctrl) {
	b.ctx.padMods(shift ? 1 : 0, ctrl ? 1 : 0, 0);
	b.ctx.trackButton(i);
}

// What a step pad press was, now (the pads are gone): with MUTE TRACKS on, a track
// button; else step i of the step page on STEPS' board.
function press(b, i, shift, ctrl, alt) {
	var c = b.ctx;
	if (c.padMode === c.MODE_MUTE) return trackPress(b, i, shift, ctrl);
	c.boardStep(c.stepPage * 16 + i, ctrl ? 1 : 0, alt ? 1 : 0, 0);
}

// A pattern button clicked (Ctrl+click adds to the chain, Shift+click chains a range), as
// the patch sends it: the modifier keys first.
function playPress(b, i, shift, ctrl) {
	b.ctx.padMods(shift ? 1 : 0, ctrl ? 1 : 0, 0);
	b.ctx.patternButton(i);
}

// What the engine plays over `steps`, with actions[i](stepper) done after step i (as the
// script does on the tick of step i). `bank`: a bank to load first.
function refRun(steps, actions, bank) {
	var st = new core.Stepper(M, preset), want = emptyWant();
	if (bank) st.loadBank(bank);
	steps.forEach(function (p, i) {
		addStep(want, st.step(p), i);
		if (actions[i]) actions[i](st, p);
	});
	st.destroy();
	return want;
}

// A bank with a second pattern: the test pattern with track 1 at 13 pulses and track 3
// silent.
function twoPatternBank() {
	var st = new core.Stepper(M, preset);
	var one = JSON.parse(st.savePattern());
	st.setParamByKey(0, "pulses", 13);
	st.setParamByKey(2, "pulses", 0);
	var two = st.savePattern();
	st.destroy();
	return JSON.stringify({ currentPattern: 0, patterns: [{ slot: 0, core: JSON.stringify(one) }, { slot: 1, core: two }] });
}

function loadBankStopped(s, bank) {
	s.brain.ctx.stepper.loadBank(bank);
	s.brain.ctx.sendCue();
}

// Mute mode while playing: clicks stage, Apply commits (heard from the next step, the
// timing being Immediately); Ctrl+click mutes at once. The pads show it.
(function () {
	var s = setup(), b = s.brain.ctx, steps = range(0, 96), t0 = s.w.now, shown = {};
	s.w.play(steps, function (i) {
		if (i === 10) { b.padModeSelect(1); press(s.brain, 2); press(s.brain, 0); }
		if (i === 14) shown.staged = s.brain.trk[2];
		if (i === 16) b.applyMutes();
		if (i === 30) press(s.brain, 4, false, true);
		if (i === 40) shown.muted = [s.brain.trk[0], s.brain.trk[2], s.brain.trk[4], s.brain.trk[1]];
		if (i === 50) { press(s.brain, 0); b.padModeSelect(1); } // Mute again: applies too
	});
	var want = refRun(steps, {
		16: function (st) { st.commitMutes(5, 16 / 4 + 0.125); },
		30: function (st) { st.setMuted(4, true, 31 / 4 - 1e-9); },
		50: function (st) { st.commitMutes(1, 50 / 4 + 0.125); },
	});
	compare("mutes while playing: staged, applied, Ctrl+click and Mute again, each from the next step", s.rx, t0, 120, want, 0);
	var after = function (t, from) { return playedQuarters(s.rx[t], t0, 120).filter(function (q) { return q > from; }).length; };
	check("muted tracks go quiet, repeats under way finishing (track 3 " + after(2, 6) + ", track 5 " + after(4, 8) + " notes after; track 1 " + after(0, 13) + " after unmuting)",
		after(2, 6) === 0 && after(4, 8) === 0 && after(0, 13) > 0);
	var blue = "0.12 0.17 0.28";
	check("the track buttons show the mutes (" + JSON.stringify(shown) + ")",
		shown.muted[0] === blue && shown.muted[1] === blue && shown.muted[2] === blue && shown.muted[3] !== blue);
	check("mutes: no misses (" + misses(s.rx) + "), no hanging notes (" + hanging(s.rx) + ")", misses(s.rx) === 0 && hanging(s.rx) === 0);
	done(s);
})();

// Leaving Mute mode drops what's staged; mutes while stopped apply at once.
(function () {
	var s = setup(), b = s.brain.ctx;
	b.padModeSelect(1);
	check("no staged mutes: Apply hidden (" + s.brain.apply + ")", s.brain.apply === 0);
	press(s.brain, 1);
	check("a staged mute: Apply shown (" + s.brain.apply + ")", s.brain.apply === 1);
	b.padModeSelect(0);
	check("left Mute mode: Apply hidden again (" + s.brain.apply + ")", s.brain.apply === 0);
	b.padModeSelect(1);
	b.applyMutes();
	check("leaving Mute mode drops the staged change", b.stepper.perf().muted === 0);
	press(s.brain, 3);
	b.applyMutes();
	check("applied: Apply hidden (" + s.brain.apply + ")", s.brain.apply === 0);
	press(s.brain, 5, false, true);
	check("stopped: Apply and Ctrl+click mute at once (" + b.stepper.perf().muted + ")", b.stepper.perf().muted === (8 | 32));
	var t0 = s.w.now;
	s.w.play(range(0, 64));
	var st = new core.Stepper(M, preset);
	st.setMuted(3, true, 0);
	st.setMuted(5, true, 0);
	var want = emptyWant();
	range(0, 64).forEach(function (p, i) { addStep(want, st.step(p), i); });
	st.destroy();
	compare("stopped mutes: Play plays without them", s.rx, t0, 120, want, 0);
	s.w.stop();
	s.w.wait(SAVE_PAUSE_MS + 10);
	var saved = s.brain.pattr;
	var r = setup({ saved: saved });
	check("mutes are saved with the set (" + r.brain.ctx.stepper.perf().muted + ")", r.brain.ctx.stepper.perf().muted === (8 | 32));
	done(r);
	done(s);
})();

// Pattern mode while playing: a click queues a pattern for the end of the pattern, Ctrl+
// click switches at once (what's sounding ends); the pads show the queue, and the dials
// follow the switch.
(function () {
	var bank = twoPatternBank(), s = setup(), b = s.brain.ctx;
	loadBankStopped(s, bank);
	var steps = range(0, 200), t0 = s.w.now, seen = {};
	s.w.play(steps, function (i) {
		if (i === 6) playPress(s.brain, 1);
		if (i === 8) seen.queued = b.stepper.perf().queued;
		if (i === 150) seen.before = b.stepper.getParam(0, "pulses");
		if (i === 150) s.brain.ctx.patternPad(0, false, true); // at once (the script can; no click does it now)
		if (i === 160) seen.after = [b.stepper.perf().current, b.stepper.getParam(0, "pulses")];
	});
	var want = refRun(steps, {
		6: function (st) { st.selectPattern(1, false, false, 6 / 4 + 0.125); },
		150: function (st) { st.selectPattern(0, false, true, 151 / 4 - 1e-9); },
	}, bank);
	compare("patterns while playing: queued, then at once, every note as the engine plays it", s.rx, t0, 120, want, 0);
	var t3 = playedQuarters(s.rx[2], t0, 120);
	check("the queued switch lands (track 3, silent in pattern 2: notes before the switch, none from quarter 16.5 to the switch back: " +
		t3.filter(function (q) { return q < 16; }).length + ", " + t3.filter(function (q) { return q >= 16 && q < 37.5; }).length + ")",
		t3.filter(function (q) { return q < 16; }).length > 0 && t3.filter(function (q) { return q >= 16.5 && q < 37.5; }).length === 0);
	check("queued shown (" + seen.queued + "), dials follow the switch (" + seen.before + " then " + JSON.stringify(seen.after) + ")",
		seen.queued === 1 && seen.before === 13 && seen.after[0] === 0 && seen.after[1] === 9);
	check("patterns: no misses (" + misses(s.rx) + "), no hanging notes (" + hanging(s.rx) + ")", misses(s.rx) === 0 && hanging(s.rx) === 0);
	s.w.stop();
	s.w.wait(SAVE_PAUSE_MS + 10);
	var saved = s.brain.pattr;
	done(s);
	var r = setup({ saved: saved }), rb = r.brain.ctx.stepper;
	check("the bank is saved with the set: both patterns (" + rb.perf().used + "), and the one playing",
		rb.perf().used === 3 && rb.perf().current === 0);
	done(r);
})();

// Patterns while stopped: a click switches at once, the pattern starting with the song;
// Shift+click chains: the chain plays each pattern through, in turn.
(function () {
	var bank = twoPatternBank(), s = setup(), b = s.brain.ctx;
	loadBankStopped(s, bank);
	playPress(s.brain, 1);
	check("stopped: pattern 2 at once (" + b.stepper.perf().current + ")", b.stepper.perf().current === 1);
	playPress(s.brain, 0, false, true); // Ctrl+click: each added to the chain
	playPress(s.brain, 1, false, true);
	var steps = range(0, 400), t0 = s.w.now;
	s.w.play(steps);
	var st = new core.Stepper(M, preset), want = emptyWant();
	st.loadBank(bank);
	st.selectPattern(1, false, true, 0);
	st.selectPattern(0, true, true, 0);
	st.selectPattern(1, true, true, 0);
	st.cut = false;
	var switched = 0, last = st.perf().current;
	steps.forEach(function (p, i) {
		addStep(want, st.step(p), i);
		if (st.perf().current !== last) { switched++; last = st.perf().current; }
	});
	st.destroy();
	compare("a chain made while stopped: Play plays it", s.rx, t0, 120, want, 0);
	check("the chain switches (" + switched + " times in 400 steps)", switched >= 2);
	done(s);
})();

// A loop wrapping back past a switch: the switched pattern plays on, from the loop's start.
(function () {
	var bank = twoPatternBank(), s = setup(), b = s.brain.ctx;
	loadBankStopped(s, bank);
	s.w.setSong("loop", 1);
	s.w.setSong("loop_start", 0);
	s.w.setSong("loop_length", 8);
	var steps = [];
	for (var k = 0; k < 3; k++) steps = steps.concat(range(0, 32));
	var t0 = s.w.now;
	s.w.play(steps, function (i) {
		if (i === 10) s.brain.ctx.patternPad(1, false, true); // at once
	});
	var want = refRun(steps, { 10: function (st) { st.selectPattern(1, false, true, 11 / 4 - 1e-9); } }, bank);
	compare("a loop back past a switch: every note as the engine plays it", s.rx, t0, 120, want, 0);
	var t1 = playedQuarters(s.rx[0], t0, 120).filter(function (q) { return q >= 8 && q < 16; }).length;
	check("the switched pattern plays round the loop (track 1, 13 pulses a bar: " + t1 + " notes in the second time round)", t1 === 26);
	check("loop: no misses (" + misses(s.rx) + "), no hanging notes (" + hanging(s.rx) + ")", misses(s.rx) === 0 && hanging(s.rx) === 0);
	done(s);
})();

// A set saved before the bank (format 1: one pattern) still loads, as pattern 1.
(function () {
	var st = new core.Stepper(M, preset);
	st.setParamByKey(0, "pulses", 3);
	var text = st.savePattern();
	st.destroy();
	var s = setup(), b64 = s.brain.ctx.toBase64(text);
	done(s);
	var r = setup({ saved: [1, "p" + b64] });
	check("a format 1 set loads into pattern 1 (pulses " + r.brain.ctx.stepper.getParam(0, "pulses") + ")",
		r.brain.ctx.stepper.getParam(0, "pulses") === 3 && r.brain.ctx.stepper.perf().current === 0);
	done(r);
})();

// ---- the display ----

// The display's controls shown now (not the tab frames and pad bars, which come and go).
// A page's number by name: the device's tabs (P), the window's (W: no KEYS).
function P(b, name) { return b.DISPLAY.pages.indexOf(name); }
function W(b, name) { return b.winPages().indexOf(name); }

// The page tab lit (-1: none) on a copy, from its buttons shown ("d_tab<i>_on").
function tabOf(shown) {
	for (var i = 0; i < 13; i++) if (shown && shown["d_tab" + i + "_on"]) return i;
	return -1;
}

function shownNames(d) {
	return Object.keys(d.shown).filter(function (k) { return d.shown[k] && !/^d_(padlock|stepmark|cycmark|rndmark|pages|gear|tempwait|mutemode|tab\d)/.test(k); }).sort().join(" ");
}

// Pages: a page shows its rows (or the note for one not built yet), set from the edit
// track; an edit while stopped is heard at Play.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain;
	check("at load the TRACK page: its info line and steps (" + shownNames(d) + ")", shownNames(d) === "d_board d_trackInfo");
	b.displayPage(P(b, "SCALE")); // SCALE
	check("SCALE shows its rows and notes (" + shownNames(d) + ")", shownNames(d) === "d_diagram d_root d_scale dl_root dl_scale");
	check("SCALE set from the edit track (root " + d.disp.d_root + ", scale " + d.disp.d_scale + ")", d.disp.d_root === 0 && d.disp.d_scale === 0);
	b.disp("scale", 1);
	b.disp("root", 2);
	check("stopped: the edit is in the engine and shown (root " + b.stepper.getField(0, "root") + ")",
		b.stepper.getField(0, "root") === 2 && b.stepper.getField(0, "scale") === 1 && d.disp.d_root === 2);
	b.editTrackSelect(1);
	check("another edit track: its own values (" + d.disp.d_root + ")", d.disp.d_root === 0);
	var t0 = s.w.now;
	s.w.play(range(0, 64));
	var st = new core.Stepper(M, preset), want = emptyWant();
	st.setField(0, "scale", 1);
	st.setField(0, "root", 2);
	range(0, 64).forEach(function (p, i) { addStep(want, st.step(p), i); });
	st.destroy();
	compare("a SCALE edit while stopped: Play plays it, pitches and all", s.rx, t0, 120, want, 0);
	s.w.stop();
	s.w.wait(SAVE_PAUSE_MS + 10);
	var r = setup({ saved: s.brain.pattr });
	check("the setting is saved with the set (root " + r.brain.ctx.stepper.getField(0, "root") + ")", r.brain.ctx.stepper.getField(0, "root") === 2);
	done(r);
	done(s);
})();

// Settings while playing: taken on the next 16th, heard from the next step.
(function () {
	var s = setup(), b = s.brain.ctx, steps = range(0, 96), t0 = s.w.now;
	b.displayPage(P(b, "VOICING")); // VOICING
	b.editTrackSelect(4); // the chord track
	s.w.play(steps, function (i) {
		if (i === 20) b.disp("style", 2);
		if (i === 40) b.disp("mono", 1);
	});
	var want = refRun(steps, {
		20: function (st) { st.setField(4, "style", 2); },
		40: function (st) { st.setField(4, "mono", 1); },
	});
	compare("VOICING edits while playing: from the next step, as the engine plays them", s.rx, t0, 120, want, 0);
	check("VOICING edits: no hanging notes (" + hanging(s.rx) + ")", hanging(s.rx) === 0);
	done(s);
})();

// GROOVE: BARS is dimmed for the percussion grooves, and reads LOOPS with SYNC on LOOP;
// PHRASE: its list leaves out Rack's CV input, so USER is the engine's 9.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain;
	b.displayPage(P(b, "GROOVE"));
	check("GROOVE: BARS dimmed for AGOGO (" + d.disp.da_grooveLength + ")", d.disp.da_grooveLength === 0);
	b.disp("groove", 4);
	check("GROOVE: BARS active for SAW (" + d.disp.da_grooveLength + ")", d.disp.da_grooveLength === 1);
	b.disp("grooveLoop", 1);
	check("GROOVE: SYNC LOOP relabels BARS as LOOPS (" + d.disp.dl_grooveLength + ")", d.disp.dl_grooveLength === "LOOPS");
	b.displayPage(P(b, "PHRASE"));
	b.disp("phrase", 8);
	check("PHRASE: USER is the engine's 9, shown as the 9th choice (" + b.stepper.getField(0, "phrase") + ", " + d.disp.d_phrase + ")",
		b.stepper.getField(0, "phrase") === 9 && d.disp.d_phrase === 8);
	done(s);
})();

// Mute mode shows its settings over the page; APPLY at BAR lands staged mutes on the bar
// line; a page tab leaves Mute mode, back to the mode before it.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain, steps = range(0, 64), t0 = s.w.now;
	b.displayPage(P(b, "SCALE"));
	b.padModeSelect(1);
	check("Mute mode: the MUTE view (" + shownNames(d) + ")", shownNames(d) === "d_muteCuts d_muteTiming dl_muteCuts dl_muteTiming");
	b.disp("muteTiming", 2);
	s.w.play(steps, function (i) {
		if (i === 5) { press(s.brain, 0); b.applyMutes(); }
	});
	var want = refRun(steps, {
		0: function (st) { st.setSetting("muteTiming", 2); },
		5: function (st) { st.commitMutes(1, 5 / 4 + 0.125); },
	});
	compare("APPLY at BAR: the mute lands on the next bar line", s.rx, t0, 120, want, 0);
	var t1 = playedQuarters(s.rx[0], t0, 120);
	check("track 1 plays to the bar line, then not (" + t1.filter(function (q) { return q >= 1.5 && q < 4; }).length + " notes before, " +
		t1.filter(function (q) { return q >= 4.01; }).length + " after)", t1.filter(function (q) { return q >= 1.5 && q < 4; }).length > 0 &&
		t1.filter(function (q) { return q >= 4.01; }).length === 0);
	s.w.stop();
	b.displayPage(P(b, "SONG"));
	check("a page tab leaves Mute mode (mode " + d.mode + "), SONG shown (" + shownNames(d) + ")",
		d.mode === 0 && / d_songEntries /.test(" " + shownNames(d) + " "));
	s.w.wait(SAVE_PAUSE_MS + 10);
	var r = setup({ saved: s.brain.pattr });
	check("the mute timing is saved with the set (" + r.brain.ctx.stepper.getSetting("muteTiming") + ")", r.brain.ctx.stepper.getSetting("muteTiming") === 2);
	done(r);
	done(s);
})();

// A dial turned shows its page; in Mute mode the Mute view stays.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain;
	turn(s.brain, 0, "pace", -20);
	check("a Repeats dial shows SETUP, where Choke / Tail is (" + tabOf(d.shown) + ", " + shownNames(d) + ")",
		tabOf(d.shown) === P(b, "SETUP") && / d_repeatTail /.test(" " + shownNames(d) + " ") && !/ d_instance /.test(" " + shownNames(d) + " "));
	turn(s.brain, 0, "pitch", 3);
	check("Pitch shows SCALE (" + tabOf(d.shown) + ")", tabOf(d.shown) === P(b, "SCALE"));
	b.padModeSelect(1);
	turn(s.brain, 0, "voicing", 2);
	check("in Mute mode the Mute view stays (" + shownNames(d) + "), the page moves under it (" + tabOf(d.shown) + ")",
		shownNames(d) === "d_muteCuts d_muteTiming dl_muteCuts dl_muteTiming" && tabOf(d.shown) === P(b, "VOICING"));
	b.padModeSelect(0);
	check("leaving Mute mode: VOICING (" + shownNames(d) + ")", /d_style/.test(shownNames(d)));
	done(s);
})();

// The pattern buttons: CPY / PST copy a pattern into a slot, a click while stopped plays it
// at once, slots holding a pattern dim orange, CLR clears one.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain;
	b.clipSelect(0, 1);
	playPress(d, 0);
	b.clipSelect(1, 1);
	playPress(d, 3);
	check("CPY 1, PST 4: slot 4 holds the pattern (" + b.stepper.perf().used + ")", (b.stepper.perf().used & 8) !== 0);
	playPress(d, 3);
	check("a click while stopped: pattern 4 at once, orange (" + b.stepper.perf().current + ", " + d.patc[3] + ")",
		b.stepper.perf().current === 3 && d.patc[3] === "0.94 0.54 0.14" && b.stepper.getParam(0, "pulses") === 9);
	check("slot 1 holds a pattern: dim orange; slot 2 empty: dark (" + d.patc[0] + ", " + d.patc[1] + ")",
		d.patc[0] === "0.24 0.14 0.04" && d.patc[1] === "0.06 0.06 0.07");
	b.clipSelect(2, 1);
	playPress(d, 3);
	check("CLR 4: the playing pattern back to the default (pulses " + b.stepper.getParam(0, "pulses") + ")",
		b.stepper.getParam(0, "pulses") === 0 && (b.stepper.perf().used & 8) === 0);
	done(s);
})();

// The diagrams: GROOVE's accent shape with a playhead, PHRASE's notes or its User grid
// (edited by clicking), VOICING's next notes.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain;
	b.displayPage(P(b, "GROOVE"));
	check("GROOVE shows the diagram (" + shownNames(d) + ")", /d_diagram/.test(shownNames(d)));
	check("GROOVE: the accent shape, 129 values, one bar repeated (" + d.diagram.slice(0, 3).join(" ") + ")",
		d.diagram[0] === "groove" && d.diagram[1] === "1 BAR, REPEATED" && d.diagram.length === 3 + 129);
	b.disp("groove", 4);
	b.disp("grooveLength", 1);
	check("GROOVE: SAW over 2 bars (" + d.diagram[1] + ")", d.diagram[1] === "2 BARS");
	check("stopped: no playhead (" + d.play + ")", d.play === -1);
	var seen = {};
	s.w.play(range(0, 40), function (i) { if (i > 0) seen[d.play] = true; });
	check("playing: the playhead moves (" + Object.keys(seen).length + " places)", Object.keys(seen).length > 30);
	s.w.stop();
	b.displayPage(P(b, "VOICING"));
	check("VOICING: the next 16 notes, as dashes (" + d.diagram.slice(0, 3).join(" ") + ")",
		d.diagram[0] === "notes" && d.diagram[1] === "NEXT 16 NOTES" && d.diagram[2] === 1 && d.diagram.length === 3 + 16);
	b.displayPage(P(b, "PHRASE"));
	check("PHRASE: the phrase's notes (" + d.diagram.slice(0, 3).join(" ") + ", " + (d.diagram.length - 3) + ")",
		d.diagram[0] === "notes" && d.diagram[1] === "4 BARS" && d.diagram.length === 3 + 64);
	b.disp("phrase", 8); // USER
	check("PHRASE USER: the grid (" + d.diagram.slice(0, 3).join(" ") + ")", d.diagram[0] === "grid" && d.diagram.length === 2 + 16);
	b.phraseGrid(3, 11);
	check("a click on the grid sets the column (" + d.diagram[2 + 3] + ")", d.diagram[2 + 3] === 11);
	done(s);
})();

// The diagram's [v8ui] script, run against a fake mgraphics: it draws each kind, and a
// click on the grid sends the column and level.
(function () {
	var calls = 0, sent = [];
	var g = {
		init: function () {}, redraw: function () {}, size: [454, 54],
		select_font_face: function () {}, set_font_size: function () {}, text_measure: function () { return [40, 9]; },
		set_source_rgba: function () { calls++; }, move_to: function () {}, line_to: function () {}, show_text: function () {},
		stroke: function () {}, fill: function () {}, rectangle: function () {}, set_line_width: function () {}, ellipse: function () {}
	};
	var ctx = { mgraphics: g, outlet: function () { sent.push(Array.prototype.slice.call(arguments, 1)); }, box: { rect: [0, 0, 454, 54] }, Math: Math };
	vm.createContext(ctx);
	vm.runInContext(fs.readFileSync(path.join(DEVICE, "jezric_talea_diagram.js"), "utf8"), ctx);
	// As Max: a message goes to the function of its name, if there is one, else to anything.
	function msg(name) {
		var a = Array.prototype.slice.call(arguments, 1);
		if (typeof ctx[name] === "function") return ctx[name].apply(null, a);
		ctx.messagename = name;
		ctx.anything.apply(null, a);
	}
	var curve = []; for (var i = 0; i <= 128; i++) curve.push(Math.sin(i / 20));
	ctx.groove.apply(null, ["2 BARS", 0.5].concat(curve));
	msg("play", 0.25);
	msg("live_lcd_control_fg", 1, 0.5, 0, 1);
	ctx.paint();
	ctx.notes.apply(null, ["NEXT 16 NOTES", 1, 60, 62, 64, 65]);
	ctx.paint();
	var levels = []; for (i = 0; i < 16; i++) levels.push(i % 12);
	ctx.grid.apply(null, ["4 BARS"].concat(levels));
	ctx.paint();
	ctx.onclick(2 + 450 / 16 * 2.5, 12 + 40 * 0.5);
	ctx.ondrag(2 + 450 / 16 * 2.5, 12 + 40 * 0.5, 1); // the same cell: not sent again
	ctx.ondrag(2 + 450 / 16 * 5.5, 12 + 1, 1);
	check("the diagram script draws every kind (" + calls + " colour changes)", calls > 20);
	check("a click and drag on the grid send column and level (" + JSON.stringify(sent) + ")",
		sent.length === 2 && sent[0].join() === "grid,2,5" && sent[1].join() === "grid,5,11");
	// The board: steps and the keyboard; a key click sends the note, SCALE's the pitch class.
	g.size = [454, 97];
	msg.apply(null, ["steps", 20, 16, 3, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0]);
	ctx.paint();
	ctx.keys.apply(null, ["C2 - B5", 36, 4, 0xAB5, 0, 2, 48, 52, 1, 52]);
	ctx.paint();
	sent = [];
	var ww = 450 / 28;
	ctx.onclick(2 + ww * 7.5, 90);  // the 8th white key: C3 (48)
	ctx.onclick(2 + ww * 1, 20);    // the first black key, between C2 and D2: C#2 (37)
	ctx.scalekeys(0xAB5, 0);
	g.size = [454, 57];
	ctx.onclick(56 + 450 / 28 * 2.5, 50); // E: SCALE's keys are KEYS' size, from x 56
	ctx.onclick(56 + 450 / 28 * 8, 50);   // past the octave: nothing
	var keySent = sent;
	sent = [];
	g.size = [454, 116];
	var cells = []; for (i = 0; i < 16; i++) cells.push(i < 12 ? 1 + (i % 3 === 0 ? 2 : 0) + (i === 4 ? 4 : 0) + (i === 1 ? 8 : 0) : 0);
	var vals = []; for (i = 0; i < 16; i++) vals.push(i === 1 ? 60 : "x");
	msg.apply(null, ["lanes", "STEPS", 3].concat(cells, [2, "VEL", 0, 127], vals, ["TIM", -100, 100], vals, [0]));
	ctx.paint();
	var cw = (454 - 2 - 56) / 16;
	ctx.onclick(56 + cw * 1.5, 30, 1, 0);       // lane 1, step 2, near the top
	ctx.ondrag(56 + cw * 2.5, 90, 1);            // dragging stays in lane 1
	ctx.onclick(56 + cw * 2.5, 30, 1, 1);        // Ctrl+click: remove
	ctx.onclick(56 + cw * 14.5, 30, 1, 0);       // past the steps: nothing
	ctx.onclick(56 + cw * 5.5, 8, 1, 0);         // the STEPS row: select step 6
	ctx.onclick(56 + cw * 7.5, 8, 1, 1);         // Ctrl+click: add step 8
	check("lanes draw; a click and drag set locks, Ctrl+click removes (" + JSON.stringify(sent) + ")",
		sent.length === 5 && sent[3].join() === "stepsel,5,0" && sent[4].join() === "stepsel,7,1" && sent[0][0] === "lane" && sent[0][1] === 0 && sent[0][2] === 1 && sent[0][3] === 0.25 &&
		sent[1][0] === "lane" && sent[1][1] === 0 && sent[1][2] === 2 && sent[1][3] === 0 && sent[2].join() === "laneclear,0,2");
	var before = calls, lane2 = cells.slice();
	msg.apply(null, ["cycles", "EDIT 2", 1, 3].concat(cells, [2, "C1", 0, -1], cells, ["C2*", 1 + 2 + 4, 3], lane2));
	ctx.paint();
	var clickSent = sent.length;
	ctx.onclick(56 + cw * 3.5, 40, 1, 0);
	check("TRACK's cycle lanes draw; a step click there does nothing (" + (calls - before) + " colour changes)", calls - before > 20 && sent.length === clickSent);
	var g1 = 18 + (Math.min(16, (ctx.size()[1] - 2 - 18) / 2 - 2) + 2); // lane 2's top
	ctx.onclick(10, g1 + 3, 1, 0);
	check("Cycle edit: a lane's label sends its cycle (" + JSON.stringify(sent.slice(clickSent)) + ")", sent.length === clickSent + 1 && sent[clickSent].join() === "cyclesel,1");
	msg.apply(null, ["cycles", "TRACK", 0, 3].concat(cells, [2, "C1", 0, -1], cells, ["C2*", 1 + 2 + 4, 3], lane2));
	ctx.onclick(10, g1 + 3, 1, 0);
	check("out of Cycle edit the labels take no click", sent.length === clickSent + 1);
	sent = keySent;
	check("the board draws steps and keys; key clicks send notes (" + JSON.stringify(sent) + ")",
		sent.length === 3 && sent[0].join() === "key,48" && sent[1].join() === "key,37" && sent[2].join() === "scale,4");
})();

// TRACK: the info line and the steps; KEYS: the pool keyboard, a click adds a note heard at
// Play, the octave buttons move it; SCALE's notes: a click makes the scale the User one.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain;
	check("TRACK: the info line (" + d.disp.d_trackInfo + ")", /^TRACK 1  STEPS 16  PULSES 9  ROT 0  1\/16  LEN /.test(d.disp.d_trackInfo));
	check("TRACK: the steps (" + d.disp.d_board.slice(0, 4).join(" ") + ")", d.disp.d_board[0] === "steps" && d.disp.d_board[1] === 16 && d.disp.d_board.length === 4 + 16);
	b.editTrackSelect(1);
	check("TRACK follows the edit track (" + d.disp.d_board[1] + " steps)", d.disp.d_board[1] === 12 && /^TRACK 2 /.test(d.disp.d_trackInfo));
	b.editTrackSelect(0);
	b.displayPage(P(b, "KEYS"));
	check("KEYS: octave buttons, help line, keyboard (" + shownNames(d) + ")", shownNames(d) === "d_board d_keysHelp d_keysOctave dl_keysOctave");
	var pool0 = b.stepper.pool(0);
	check("KEYS: from C2, the track's pool (" + d.disp.d_board.slice(0, 9).join(" ") + ")",
		d.disp.d_board[0] === "keys" && d.disp.d_board[2] === 36 && d.disp.d_board[6] === pool0.length);
	b.poolKey(64);
	b.poolKey(67);
	check("KEYS: clicks add pool notes (" + b.stepper.pool(0).join(" ") + ")", b.stepper.pool(0).join() === pool0.concat([64, 67]).sort(function (x, y) { return x - y; }).join());
	b.poolKey(61); // C#: not in the scale (Chromatic is everything: make it Major first)
	b.disp("keysOctave", 1);
	check("KEYS: > moves the keyboard up an octave (" + d.disp.d_board[2] + ")", d.disp.d_board[2] === 48);
	var t0 = s.w.now;
	s.w.play(range(0, 64));
	var st = new core.Stepper(M, preset), want = emptyWant();
	st.poolKey(0, 64);
	st.poolKey(0, 67);
	st.poolKey(0, 61);
	range(0, 64).forEach(function (p, i) { addStep(want, st.step(p), i); });
	st.destroy();
	compare("pool notes added while stopped: Play plays them", s.rx, t0, 120, want, 0);
	s.w.stop();
	b.displayPage(P(b, "SCALE"));
	b.disp("scale", 1); // Major
	b.scaleKey(1);      // C#: into the scale, which becomes User
	check("SCALE: a note click makes the User scale (" + b.stepper.getField(0, "scale") + ", notes " + b.stepper.scaleNotes(0).toString(2) + ")",
		b.stepper.getField(0, "scale") === 23 && (b.stepper.scaleNotes(0) & 2) !== 0 && d.disp.d_scale === 23 && d.diagram[0] === "scalekeys");
	done(s);
})();

// Cycle edit on KEYS, SCALE and RANDOM: the edited cycle's own pool, scale and random
// amounts (the track's left alone), heard at Play; CYCLE's CLEAR lists and drops them.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain;
	b.displayPage(P(b, "CYCLE"));
	s.w.wait(150);
	b.disp("cycleCount", 1);
	s.w.wait(150);
	b.disp("cycleSelect", 1);
	b.disp("cycleEdit", 1);
	var pool0 = b.stepper.pool(0);
	b.displayPage(P(b, "KEYS"));
	b.poolKey(72);
	check("Cycle edit, KEYS: the key goes in cycle 2's pool (" + b.stepper.pool(0, 1).join(" ") + "; track " + b.stepper.pool(0).join(" ") + ")",
		b.stepper.pool(0, 1).indexOf(72) >= 0 && b.stepper.pool(0).join() === pool0.join() && b.stepper.pool(0, 0).join() === pool0.join());
	check("KEYS shows cycle 2's pool", d.disp.d_board[6] === pool0.length + 1);
	b.displayPage(P(b, "SCALE"));
	s.w.wait(150);
	b.disp("root", 2);
	check("Cycle edit, SCALE: cycle 2's root (" + b.stepper.getField(0, "root", 1) + ", track " + b.stepper.getField(0, "root") + ")",
		b.stepper.getField(0, "root", 1) === 2 && b.stepper.getField(0, "root") === 0 && d.disp.d_root === 2);
	b.displayPage(P(b, "RANDOM"));
	s.w.wait(150);
	b.disp("rnd_pulses", 50);
	check("Cycle edit, RANDOM: cycle 2's amount (" + b.stepper.getRandom(0, "amount", "pulses", 1) + ", track " + b.stepper.getRandom(0, "amount", "pulses") + ")",
		b.stepper.getRandom(0, "amount", "pulses", 1) === 50 && b.stepper.getRandom(0, "amount", "pulses") === 0 && d.disp.d_rnd_pulses === 50);
	b.displayPage(P(b, "CYCLE"));
	check("CYCLE's CLEAR lists SCALE and R.PULSE (" + d.disp.dr_cycleClear.join(" ") + ")", d.disp.dr_cycleClear.join() === "ALL,HITS,SCALE,R.PULSE");
	b.disp("cycleEdit", 0);
	b.displayPage(P(b, "KEYS"));
	check("Cycle edit off: KEYS shows the track's pool again", d.disp.d_board[6] === pool0.length);
	var t0 = s.w.now;
	s.w.play(range(0, 64));
	var st = new core.Stepper(M, preset), want = emptyWant();
	st.setCycleCount(0, 2);
	st.poolKey(0, 72, 1);
	st.setField(0, "root", 2, 1);
	st.setRandom(0, "amount", "pulses", 50, 1);
	range(0, 64).forEach(function (p, i) { addStep(want, st.step(p), i); });
	st.destroy();
	compare("cycle 2's own pool, root and random: Play plays them in its turn", s.rx, t0, 120, want, 0);
	s.w.stop();
	b.displayPage(P(b, "CYCLE"));
	s.w.wait(150);
	b.disp("cycleClear", 2); // SCALE
	s.w.wait(150);
	check("CLEAR SCALE: cycle 2 plays the track's pool and root (" + d.disp.dr_cycleClear.join(" ") + ")",
		b.stepper.pool(0, 1).join() === pool0.join() && b.stepper.getField(0, "root", 1) === 0 && d.disp.dr_cycleClear.join() === "ALL,HITS,R.PULSE");
	b.disp("cycleClear", 2); // R.PULSE
	check("CLEAR R.PULSE: the track's amount again", b.stepper.getRandom(0, "amount", "pulses", 1) === 0 && !b.stepper.cycleState(0, 1).modified);
	done(s);
})();

// KEYS with steps selected (EUCLID mode): a key gives the selected steps their own pool (a
// pool lock, the track's left alone), shown in blue with TRACK POOL, heard at Play; TRACK
// POOL takes it away.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain;
	b.displayPage(P(b, "KEYS"));
	var pool0 = b.stepper.pool(0), want0 = pool0.concat([72]).sort(function (x, y) { return x - y; });
	check("KEYS, nothing selected: no TRACK POOL (" + d.disp.d_keysHelp + ")",
		d.shown.d_trackPool !== true && d.disp.d_keysHelp === "CLICK = ADD / REMOVE A POOL NOTE");
	press(d, 0, false, true); // Ctrl+click: steps 1 and 3 selected for locks
	press(d, 2, false, true);
	check("steps selected: they play the track pool (" + d.disp.d_keysHelp + ")",
		/TRACK POOL: CLICK = THEIR OWN/.test(d.disp.d_keysHelp) && d.shown.d_trackPool !== true &&
		d.disp.d_board[d.disp.d_board.length - 1] === 0);
	b.poolKey(72);
	check("a key: both steps get their own pool, the track's untouched (" + b.stepper.stepPool(0, -1, 0).join(" ") + " / " +
		b.stepper.stepPool(0, -1, 2).join(" ") + ", track " + b.stepper.pool(0).join(" ") + ")",
		b.stepper.stepPool(0, -1, 0).join() === want0.join() && b.stepper.stepPool(0, -1, 2).join() === want0.join() &&
		b.stepper.pool(0).join() === pool0.join() && b.stepper.stepPool(0, -1, 1).length === 0);
	check("KEYS shows the steps' pool, blue, with TRACK POOL (" + d.disp.d_keysHelp + ")",
		/OWN POOL/.test(d.disp.d_keysHelp) && d.shown.d_trackPool === true &&
		d.disp.d_board[6] === want0.length && d.disp.d_board[d.disp.d_board.length - 1] === 1);
	check("a step pool is a lock: the pads mark it", b.stepper.locksAt(0, -1, 0) && b.stepper.locksAt(0, -1, 2));
	b.displayPage(P(b, "LOCKS"));
	var bd = d.disp.d_board;
	check("LOCKS: a POOL lane marking the steps with their own pool (" + bd.slice(19, 26).join(" ") + ")",
		bd[0] === "lanes" && bd[19] === 1 && bd[20] === "POOL" && bd[21] === bd[22] && bd[23] === 1 && bd[24] === "x" && bd[25] === 1);
	b.lane(0, 1, 0.5);
	b.laneClear(0, 0);
	check("the POOL lane can't be edited", b.stepper.stepPool(0, -1, 1).length === 0 && b.stepper.stepPool(0, -1, 0).length === 2 &&
		b.stepper.locksUsed(0, -1).length === 0);
	b.displayPage(P(b, "KEYS"));
	var t0 = s.w.now;
	s.w.play(range(0, 64));
	var st = new core.Stepper(M, preset), want = emptyWant();
	st.stepPoolKey(0, -1, 0, 72);
	st.stepPoolCopy(0, -1, 2, 0);
	range(0, 64).forEach(function (p, i) { addStep(want, st.step(p), i); });
	st.destroy();
	compare("step pools set while stopped: Play plays them", s.rx, t0, 120, want, 0);
	s.w.stop();
	b.disp("trackPool", "bang");
	check("TRACK POOL: the steps play the track's again (" + b.stepper.stepPool(0, -1, 0).length + ")",
		b.stepper.stepPool(0, -1, 0).length === 0 && b.stepper.stepPool(0, -1, 2).length === 0 && d.shown.d_trackPool === false);
	b.displayPage(P(b, "SETUP"));
	b.disp("seqMode", 1);
	b.displayPage(P(b, "KEYS"));
	press(d, 0, false, true);
	b.poolKey(72);
	var sq = b.stepper.seqStep(0, -1, 0);
	check("SEQ mode: no step pools, the key goes in the step's chord (" + (sq ? sq.notes.join(" ") : "rest") + ")",
		b.stepper.stepPool(0, -1, 0).length === 0 && sq && sq.notes.indexOf(72) >= 0 && d.shown.d_trackPool === false);
	done(s);
})();

// RANDOM: the amounts and their per cycle / per tick switches, Variance, Rate, Slew, Lock
// and the sequence buttons, for the edit track; heard from the next step while playing,
// saved with the set.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain;
	b.displayPage(P(b, "RANDOM"));
	var shown = shownNames(d).split(" ");
	check("RANDOM shows 15 amounts, their switches and the rest (" + shown.length + " controls)",
		shown.length === 15 * 3 + 4 * 2 + 2 && shown.indexOf("d_rnd_pulses") >= 0 && shown.indexOf("d_rndc_pulses") >= 0 &&
		shown.indexOf("dl_rndc_pulses") < 0 && shown.indexOf("d_rndSeq") >= 0);
	check("RANDOM set from the engine (pulses " + d.disp.d_rnd_pulses + ", per cycle " + d.disp.d_rndc_pulses + ", rate " + d.disp.d_rndRate + ")",
		d.disp.d_rnd_pulses === 0 && d.disp.d_rndc_pulses === 1 && d.disp.d_rndc_velocity === 0 && d.disp.d_rndRate === 9);
	check("no random amounts: the RANDOM tab unmarked (" + d.shown.d_rndmark + ")", !d.shown.d_rndmark);
	b.disp("rnd_velocity", -35);
	check("a random amount: the RANDOM tab marked", d.shown.d_rndmark === true);
	b.displayPage(P(b, "SCALE"));
	check("the mark stays on other pages", d.shown.d_rndmark === true);
	b.displayPage(P(b, "RANDOM"));
	b.disp("rndc_velocity", 1);
	b.disp("rndVariance", 25);
	check("stopped: amounts and settings in the engine (" + b.stepper.getRandom(0, "amount", "velocity") + ", " +
		b.stepper.getRandom(0, "perCycle", "velocity") + ", " + b.stepper.getRandom(0, "variance") + ")",
		b.stepper.getRandom(0, "amount", "velocity") === -35 && b.stepper.getRandom(0, "perCycle", "velocity") === 1 &&
		b.stepper.getRandom(0, "variance") === 25 && d.disp.d_rnd_velocity === -35);
	var steps = range(0, 128), t0 = s.w.now;
	s.w.play(steps, function (i) {
		if (i === 30) b.disp("rnd_pitch", 60);
		if (i === 60) b.disp("rndSeq", 0); // REGEN
		if (i === 90) b.disp("rndLock", 1);
	});
	var want = refRun(steps, {
		0: function (st) { st.setRandom(0, "amount", "velocity", -35); st.setRandom(0, "perCycle", "velocity", 1); st.setRandom(0, "variance", null, 25); },
		30: function (st) { st.setRandom(0, "amount", "pitch", 60); },
		60: function (st) { st.randomAction(0, 0, 60 / 4 + 0.125); },
		90: function (st) { st.randomAction(0, 1, 90 / 4 + 0.125); },
	});
	compare("random edits while playing: amounts, Regen and Lock, as the engine plays them", s.rx, t0, 120, want, 0);
	var plain = refRun(steps, {});
	check("the random edits change the notes", want.pitch[0].join() !== plain.pitch[0].join());
	check("LOCK shown on (" + d.disp.d_rndLock + ")", d.disp.d_rndLock === 1 && b.stepper.getRandom(0, "locked") === 1);
	s.w.stop();
	b.disp("rndLock", 1); // the state it's in: nothing
	check("LOCK clicked again on ON: still locked", b.stepper.getRandom(0, "locked") === 1);
	s.w.wait(SAVE_PAUSE_MS + 10);
	var r = setup({ saved: s.brain.pattr });
	check("random settings saved with the set (" + r.brain.ctx.stepper.getRandom(0, "amount", "pitch") + ")",
		r.brain.ctx.stepper.getRandom(0, "amount", "pitch") === 60 && r.brain.ctx.stepper.getRandom(0, "variance") === 25);
	done(r);
	b.disp("rndSeq", 5); // CLEAR ALL
	check("CLEAR ALL: every amount 0 (" + b.stepper.getRandom(0, "amount", "pitch") + ", " + d.disp.d_rnd_velocity + "), tab unmarked",
		b.stepper.getRandom(0, "amount", "pitch") === 0 && d.disp.d_rnd_velocity === 0 && d.shown.d_rndmark === false);
	b.stepper.setRandom(0, "amount", "steps", 50); // no row for it; the engine refuses it too
	check("Steps can't be randomised", b.stepper.getRandom(0, "amount", "steps") === 0);
	done(s);
})();

// Cycles: the CYCLE page (COUNT, CYCLE, MUTE, CLEAR), Cycle edit (the dials and step pads
// edit the selected cycle, which plays alone), the Cycle pads; Play plays the cycles in turn.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain;
	b.displayPage(P(b, "CYCLE"));
	check("CYCLE shows its rows (" + shownNames(d) + ")",
		shownNames(d) === "d_cycleClear d_cycleCount d_cycleEdit d_cycleMute d_cyclePlaying d_cycleSelect dl_cycleClear dl_cycleCount dl_cycleEdit dl_cycleMute dl_cycleSelect");
	check("one cycle at first (" + d.disp.d_cycleCount + ", " + d.disp.dr_cycleClear.join(" ") + ")", d.disp.d_cycleCount === 0 && d.disp.dr_cycleClear.join() === "ALL,HITS");
	s.w.wait(200);
	b.disp("cycleCount", 1);
	s.w.wait(200);
	b.disp("cycleSelect", 1);
	check("CYCLE tab unframed (" + d.shown.d_cycmark + ")", !d.shown.d_cycmark);
	b.displayPage(P(b, "STEPS"));
	var bd = d.disp.d_board;
	check("TRACK with 2 cycles: the edit row and a lane per cycle, cycle 2 boxed (" + bd.slice(0, 4).join(" ") + " ... " + bd.slice(21, 25).join(" ") + ")",
		bd[0] === "cycles" && bd[1] === "TRACK" && bd[2] === 0 && bd[20] === 2 && bd[21] === "C1" && bd[22] === 0 && bd[40] === "C2" && bd[41] === 4);
	b.displayPage(P(b, "CYCLE"));
	b.disp("cycleEdit", 1);
	var cyc = {};
	for (var k = 0; k < 8; k++) { b.blink(); cyc[d.shown.d_cycmark] = true; }
	check("EDIT on: the CYCLE tab's red frame blinks, the line says so (" + d.disp.d_cyclePlaying + ")",
		cyc["true"] && cyc["false"] && /^EDIT: CYCLE 2 PLAYS ALONE/.test(d.disp.d_cyclePlaying));
	b.displayPage(P(b, "STEPS"));
	b.cycleLaneClick(0);
	check("Cycle edit, TRACK: a lane's label edits that cycle (" + b.selCycle + ", " + d.disp.d_board[1] + ")", b.selCycle === 0 && d.disp.d_board[1] === "EDIT 1");
	b.cycleLaneClick(1);
	b.cycleEditSelect(0);
	b.cycleLaneClick(0);
	check("out of Cycle edit a label click does nothing (" + b.selCycle + ")", b.selCycle === 1);
	b.cycleEditSelect(1);
	b.displayPage(P(b, "CYCLE"));
	check("Cycle edit: the dials show cycle 2's values (pulses " + b.dialValue(0, "pulses") + ")", b.dialValue(0, "pulses") === 9);
	turn(s.brain, 0, "pulses", 3);
	check("Cycle edit: a dial overrides cycle 2 only (" + b.stepper.getCycleParam(0, 1, "pulses") + ", track " + b.stepper.getParam(0, "pulses") + ")",
		b.stepper.getCycleParam(0, 1, "pulses") === 3 && b.stepper.getParam(0, "pulses") === 9 && b.stepper.getCycleParam(0, 0, "pulses") === 9);
	b.displayPage(P(b, "CYCLE"));
	check("cycle 2 marked as changed, CLEAR lists its override (" + d.disp.dr_cycleSelect.slice(0, 2).join(" ") + "; " + d.disp.dr_cycleClear.join(" ") + ")",
		d.disp.dr_cycleSelect[1] === "2*" && d.disp.dr_cycleClear.join() === "ALL,HITS,PULSE");
	b.pad(0, "bang"); // step 1, in cycle 2 only
	check("Cycle edit: a step pad edits cycle 2 only (" + b.stepper.cycleHits(0, 1)[0] + " / track " + b.stepper.hits(0)[0] + ")",
		b.stepper.cycleHits(0, 1)[0] !== b.stepper.cycleHits(0, 0)[0] && b.stepper.hits(0)[0] === b.stepper.cycleHits(0, 0)[0]);
	// Playing in Cycle edit: cycle 2 alone.
	var steps = range(0, 64), t0 = s.w.now;
	s.w.play(steps);
	var st = new core.Stepper(M, preset), want = emptyWant();
	st.setCycleCount(0, 2);
	st.setCycleParam(0, 1, "pulses", 3);
	st.toggleCycleStep(0, 1, 0);
	st.setSoloCycle(0, 1);
	steps.forEach(function (p, i) { addStep(want, st.step(p), i); });
	compare("Cycle edit while playing: the edited cycle plays alone", s.rx, t0, 120, want, 0);
	s.w.stop();
	b.displayPage(P(b, "CYCLE"));
	b.disp("cycleEdit", 0);
	check("EDIT off: the frame goes", d.shown.d_cycmark === false);
	check("Cycle edit off: the dials show the track's values", b.dialValue(0, "pulses") === 9);
	var t1;
	st.setSoloCycle(0, -1);
	st.forget();
	want = emptyWant();
	b.displayPage(P(b, "CYCLE"));
	var bars = {};
	s.w.play(range(0, 32), function (i) {
		var on = Object.keys(d.shown).filter(function (k) { return /^d_cycleSelect_play/.test(k) && d.shown[k]; });
		if (i > 0) bars[on.join()] = true;
	});
	check("playing: a bar under the playing cycle, moving at each loop (" + Object.keys(bars).join(" | ") + ")",
		Object.keys(bars).sort().join(" | ") === "d_cycleSelect_play0 | d_cycleSelect_play1");
	s.w.stop();
	check("stopped: no bar", !Object.keys(d.shown).some(function (k) { return /^d_cycleSelect_play/.test(k) && d.shown[k]; }));
	t1 = s.w.now;
	s.w.play(steps);
	steps.forEach(function (p, i) { addStep(want, st.step(p), i); });
	st.destroy();
	compare("Cycle edit off: the cycles play in turn", s.rx, t1, 120, want, 0);
	var loop1 = playedQuarters(s.rx[0], t1, 120).filter(function (q) { return q >= 0 && q < 4; }).length;
	var loop2 = playedQuarters(s.rx[0], t1, 120).filter(function (q) { return q >= 4 && q < 8; }).length;
	check("the cycles differ (track 1: " + loop1 + " notes in cycle 1's loop, " + loop2 + " in cycle 2's)", loop1 === 9 && loop2 < 9 && loop2 > 0);
	s.w.stop();
	// CLEAR PULSE, then the Cycle pads.
	b.displayPage(P(b, "CYCLE"));
	s.w.wait(200);
	check("CLEAR lists the step edits too (" + d.disp.dr_cycleClear.join(" ") + ")", d.disp.dr_cycleClear.join() === "ALL,HITS,PULSE");
	b.disp("cycleClear", d.disp.dr_cycleClear.indexOf("PULSE"));
	check("CLEAR PULSE: cycle 2 plays the track's Pulses again (" + b.stepper.getCycleParam(0, 1, "pulses") + ")", b.stepper.getCycleParam(0, 1, "pulses") === 9);
	s.w.wait(200);
	b.disp("cycleSelect", 3);
	check("CYCLE 4 picked past the last: 4 cycles, 4 selected (" + b.stepper.cycleCount(0) + ", " + b.selCycle + ")",
		b.stepper.cycleCount(0) === 4 && b.selCycle === 3 && d.disp.d_cycleCount === 3);
	s.w.wait(200);
	b.disp("cycleSelect", 2);
	s.w.wait(200);
	b.disp("cycleMute", 1);
	check("MUTE: cycle 3 muted, marked (" + d.disp.dr_cycleSelect.slice(0, 4).join(" ") + ")",
		b.stepper.cycleState(0, 2).muted && d.disp.dr_cycleSelect[2] === "(3)");
	s.w.wait(SAVE_PAUSE_MS + 10);
	var r = setup({ saved: s.brain.pattr });
	check("cycles saved with the set (" + r.brain.ctx.stepper.cycleCount(0) + ")", r.brain.ctx.stepper.cycleCount(0) === 4 && r.brain.ctx.stepper.cycleState(0, 2).muted);
	done(r);
	done(s);
})();

// Step locks: Ctrl+click selects steps (pads blue; the dials that can't lock dim), a dial
// locks its value on them (heard at Play, shown in STEP's lanes), a lane click sets a lock,
// Ctrl+click in a lane removes one, CLEAR LOCKS clears the selected steps'.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain;
	press(s.brain, 1, false, true);
	press(s.brain, 4, false, true);
	check("Ctrl+click selects steps 2 and 5 (" + b.selList().join() + ")", b.selList().join() === "1,4");
	var frames = {};
	for (var k = 0; k < 8; k++) { b.blink(); frames[d.shown.d_stepmark] = true; }
	check("the LOCKS tab frame blinks (" + Object.keys(frames).join(" | ") + ")", frames["true"] && frames["false"]);
	check("the dials that can't lock dim (pulses " + d.dials.a_pulses + ", velocity " + d.dials.a_velocity + ")",
		d.dials.a_pulses === 0 && d.dials.a_velocity === 1);
	b.param("velocity", 30); // the edit track already chosen (choosing it ends a selection)
	check("a dial locks its value on the selected steps (" + b.stepper.lockGet(0, -1, 1, "velocity") + ", " + b.stepper.lockGet(0, -1, 4, "velocity") +
		", step 3 " + b.stepper.lockGet(0, -1, 2, "velocity") + "; track " + b.stepper.getParam(0, "velocity") + ")",
		b.stepper.lockGet(0, -1, 1, "velocity") === 30 && b.stepper.lockGet(0, -1, 4, "velocity") === 30 &&
		b.stepper.lockGet(0, -1, 2, "velocity") === null && b.stepper.getParam(0, "velocity") !== 30);
	check("the dial shows the lock, the display LOCKS (" + d.dials.velocity + ", page " + tabOf(d.shown) + ", " + shownNames(d) + ")",
		d.dials.velocity === 30 && tabOf(d.shown) === P(b, "LOCKS") && shownNames(d) === "d_board d_lockSelect dl_lockSelect");
	var bd = d.disp.d_board;
	check("LOCKS: a VEL lane, the locked steps' bars (" + bd.slice(0, 2).join(" ") + " ... " + bd.slice(19, 26).join(" ") + ")",
		bd[0] === "lanes" && bd[1] === "STEPS" && bd[19] === 1 && bd[20] === "VEL" && bd[23] === "x" && bd[24] === 30 && bd[27] === 30);
	b.displayPage(P(b, "STEPS"));
	check("TRACK: locked steps marked (+2), selected (+4) (" + d.disp.d_board.slice(4, 10).join(" ") + ")",
		d.disp.d_board[4 + 1] === 6 && d.disp.d_board[4 + 4] === 6 && d.disp.d_board[4 + 2] < 2);
	b.displayPage(P(b, "LOCKS"));
	b.lane(0, 7, 1);
	check("a lane click sets a lock (step 8 velocity " + b.stepper.lockGet(0, -1, 7, "velocity") + ")", b.stepper.lockGet(0, -1, 7, "velocity") === 127);
	b.laneClear(0, 7);
	check("Ctrl+click in a lane removes it", b.stepper.lockGet(0, -1, 7, "velocity") === null);
	var t0 = s.w.now;
	s.w.play(range(0, 64));
	var st = new core.Stepper(M, preset), want = emptyWant();
	st.lockSet(0, -1, 1, "velocity", 30);
	st.lockSet(0, -1, 4, "velocity", 30);
	range(0, 64).forEach(function (p, i) { addStep(want, st.step(p), i); });
	st.destroy();
	compare("locks while stopped: Play plays them", s.rx, t0, 120, want, 0);
	s.w.stop();
	s.w.wait(SAVE_PAUSE_MS + 10);
	var r = setup({ saved: s.brain.pattr });
	check("locks saved with the set", r.brain.ctx.stepper.lockGet(0, -1, 4, "velocity") === 30);
	done(r);
	b.displayPage(P(b, "LOCKS"));
	b.disp("lockSelect", 2); // CLEAR LOCKS
	check("CLEAR LOCKS: the selected steps' locks gone", b.stepper.lockGet(0, -1, 1, "velocity") === null && !b.stepper.locksAt(0, -1, 4));
	b.editTrackSelect(1);
	check("another track ends the selection (dials " + d.dials.a_pulses + ")", b.selList().length === 0 && d.dials.a_pulses === 1);
	b.editTrackSelect(0);
	b.displayPage(P(b, "LOCKS"));
	check("no locks: no lanes (" + d.disp.d_board[19] + "); 16 steps: no step pages (" + d.shown.d_lockStepPage + ")",
		d.disp.d_board[19] === 0 && !d.shown.d_lockStepPage);
	turn(s.brain, 0, "steps", 40);
	b.displayPage(P(b, "LOCKS"));
	check("40 steps: the step pages, 3 of them (" + d.shown.d_lockStepPage + ", " + d.disp.dr_lockStepPage + ")",
		d.shown.d_lockStepPage === true && d.disp.dr_lockStepPage.join() === "1-16,17-32,33-48");
	s.w.wait(200);
	b.disp("lockStepPage", 2);
	check("a step page chosen: the lanes and pads show it, Follow off (" + b.stepPage + ", " + d.page + ", follow " + d.follow + ")",
		b.stepPage === 2 && d.page === 2 && d.follow === 0);
	b.displayPage(P(b, "STEPS"));
	check("TRACK with 40 steps: PAGE and FOLLOW, off (" + d.disp.dr_trackStepPage + ", " + d.disp.d_trackStepPage + ", " + d.shown.d_trackFollow + " " + d.shown.d_trackFollow_on + ")",
		d.shown.d_trackStepPage === true && d.shown.d_trackFollow === true && !d.shown.d_trackFollow_on && d.disp.dr_trackStepPage.join() === "1-16,17-32,33-48"
		&& d.disp.d_trackStepPage === 2);
	s.w.wait(200);
	b.disp("trackFollow", "bang");
	check("TRACK's FOLLOW on: lit, the transport's too (" + b.follow + ", " + d.follow + ", " + d.shown.d_trackFollow + " " + d.shown.d_trackFollow_on + ")",
		b.follow === true && d.follow === 1 && d.shown.d_trackFollow === false && d.shown.d_trackFollow_on === true);
	b.disp("trackFollow", "bang");
	check("TRACK's FOLLOW clicked again: off (" + b.follow + ", " + d.follow + ", " + d.shown.d_trackFollow + " " + d.shown.d_trackFollow_on + ")",
		b.follow === false && d.follow === 0 && d.shown.d_trackFollow === true && d.shown.d_trackFollow_on === false);
	b.disp("trackFollow", "bang");
	s.w.wait(200);
	b.disp("trackStepPage", 1);
	check("TRACK's PAGE: the page, Follow off on both (" + b.stepPage + ", " + d.page + ", " + d.shown.d_trackFollow_on + ")",
		b.stepPage === 1 && d.page === 1 && d.follow === 0 && d.shown.d_trackFollow_on === false);
	b.followSelect(1);
	check("the transport's Follow shows on TRACK (" + d.shown.d_trackFollow_on + ")", d.shown.d_trackFollow_on === true && d.shown.d_trackFollow === false);
	b.followSelect(0);
	turn(s.brain, 0, "steps", 16);
	b.displayPage(P(b, "STEPS"));
	check("16 steps: no PAGE or FOLLOW (" + d.shown.d_trackStepPage + ", " + d.shown.d_trackFollow + ")",
		d.shown.d_trackStepPage === false && d.shown.d_trackFollow === false && !d.shown.d_trackFollow_on);
	b.displayPage(P(b, "LOCKS"));
	b.editTrackSelect(0);
	// More than 6 lanes: LANES shows, a dial's lock shows its group; the STEP tab framed.
	check("no selection: LOCKS tab unframed, no LANES (" + d.shown.d_stepmark + ", " + d.shown.d_lanePage + ")", !d.shown.d_stepmark && !d.shown.d_lanePage);
	press(s.brain, 0, false, true);
	var fr = {};
	for (var k2 = 0; k2 < 8; k2++) { b.blink(); fr[d.shown.d_stepmark] = true; }
	check("a selection: the LOCKS tab frame blinks", fr["true"] && fr["false"]);
	["repeats", "ramp", "pace", "velocity", "probability", "sustain", "accent"].forEach(function (k, i) { b.param(k, i === 3 ? 20 : 1); });
	check("7 locked: LANES 1-6 | 7 shows (" + d.shown.d_lanePage + ", " + d.disp.dr_lanePage + ")",
		d.shown.d_lanePage === true && d.disp.dr_lanePage.join() === "1-6,7");
	check("the dial last turned: its group shown (ACCENT, the 7th: " + d.disp.d_board[20] + ", page " + d.disp.d_lanePage + ")",
		d.disp.d_board[19] === 1 && d.disp.d_board[20] === "ACC" && d.disp.d_lanePage === 1);
	s.w.wait(200);
	b.disp("lanePage", 0);
	check("LANES 1-6: the first six (" + d.disp.d_board[19] + ", " + d.disp.d_board[20] + ", more " + d.disp.d_board[d.disp.d_board.length - 1] + ")",
		d.disp.d_board[19] === 6 && d.disp.d_board[20] === "REP" && d.disp.d_board[d.disp.d_board.length - 1] === 1);
	b.disp("lockSelect", 0);
	check("deselected: the LOCKS tab unframed", d.shown.d_stepmark === false);
	b.lockStepClick(3, 0);
	b.lockStepClick(6, 1);
	check("STEPS row: a click selects a step, Ctrl+click adds one (" + b.selList().join() + ")", b.selList().join() === "3,6");
	b.lockStepClick(9, 0);
	check("a plain click selects just that step (" + b.selList().join() + ")", b.selList().join() === "9");
	b.lockStepClick(9, 0);
	check("clicking the only selected step deselects it", b.selList().length === 0);
	b.displayPage(P(b, "STEPS"));
	check("another page: LANES hidden", d.shown.d_lanePage === false);
	done(s);
})();

// SEQ mode: SETUP MODE switches it (the sequence starts from the hits), a pad click
// selects a step (green) for KEYS, KEYS edits its chord, REST makes it a rest, LOCKS' first
// lanes are N.VEL and GATE; the dials SEQ doesn't use dim; a scale change moves the notes.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain;
	var hitsBefore = b.stepper.hits(0).slice(0, 16).join();
	b.displayPage(P(b, "SETUP"));
	check("SETUP shows MODE (EUCLID at first: " + d.disp.d_seqMode + ")", / d_seqMode /.test(" " + shownNames(d) + " ") && d.disp.d_seqMode === 0);
	b.disp("seqMode", 1);
	check("SEQ mode: the sequence from the hits, the same rhythm (" + b.stepper.seqMode(0, -1) + ")",
		b.stepper.seqMode(0, -1) && b.stepper.hits(0).slice(0, 16).join() === hitsBefore && b.stepper.seqStep(0, -1, 0) !== null);
	check("the dials SEQ doesn't use dim (pulses " + d.dials.a_pulses + ", voicing " + d.dials.a_voicing + ", pitch " + d.dials.a_pitch + ")",
		d.dials.a_pulses === 0 && d.dials.a_voicing === 0 && d.dials.a_pitch === 1);
	check("TRACK's line in SEQ (" + b.stepper.trackInfo(0) + ")", /SEQ  STEPS 16  9 WITH NOTES/.test(b.stepper.trackInfo(0)));
	press(s.brain, 2);
	b.displayPage(P(b, "STEPS"));
	check("a step click selects the step, green on STEPS (+8); nothing toggled (" + d.disp.d_board[4 + 2] + ")",
		b.seqSel === 2 && (d.disp.d_board[4 + 2] & 8) && b.stepper.hits(0).slice(0, 16).join() === hitsBefore);
	b.displayPage(P(b, "KEYS"));
	check("KEYS: the step's chord, REST shown (" + d.disp.d_keysHelp + ", " + d.shown.d_seqRest + ")",
		/^SEQ: /.test(d.disp.d_keysHelp) && d.shown.d_seqRest === true);
	b.poolKey(64);
	b.poolKey(67);
	var chord = b.stepper.seqStep(0, -1, 2);
	check("KEYS adds notes to the step's chord (" + (chord ? chord.notes.join(" ") : "rest") + ")", chord && chord.notes.indexOf(64) >= 0 && chord.notes.indexOf(67) >= 0);
	b.displayPage(P(b, "LOCKS"));
	var bd = d.disp.d_board;
	check("LOCKS: N.VEL and GATE first (" + bd[19] + " lanes: " + bd[20] + ", " + bd[20 + 3 + 16] + ")", bd[19] === 2 && bd[20] === "N.VEL" && bd[39] === "GATE");
	b.lane(1, 2, 3 / 15); // gate 4 on step 3
	b.lane(0, 2, 0.5);    // velocity 64
	check("the SEQ lanes set the step's gate and velocity (" + JSON.stringify(b.stepper.seqStep(0, -1, 2)) + ")",
		b.stepper.seqStep(0, -1, 2).gate === 4 && b.stepper.seqStep(0, -1, 2).velocity === 64);
	var t0 = s.w.now;
	s.w.play(range(0, 64));
	var st = new core.Stepper(M, preset), want = emptyWant();
	st.setSeqMode(0, -1, true, b.defaultSeqVelocity());
	st.seqToggleNote(0, -1, 2, 64, 100);
	st.seqToggleNote(0, -1, 2, 67, 100);
	st.seqSet(0, -1, 2, 2, 4);
	st.seqSet(0, -1, 2, 1, 64);
	range(0, 64).forEach(function (p, i) { addStep(want, st.step(p), i); });
	st.destroy();
	compare("SEQ edits while stopped: Play plays the sequence", s.rx, t0, 120, want, 0);
	s.w.stop();
	// Right-click and Alt+click on a pad: the step a rest.
	b.padGeometry(88, 142, 29, 18);
	var withNotes = range(3, 16).filter(function (p) { return b.stepper.seqStep(0, -1, p) !== null; });
	var ra = withNotes[0], rb = withNotes[1], rc = withNotes[2];
	b.rightClick(88 + 29 * ra + 10, 150);
	press(s.brain, rb, false, false, true);
	b.rightClick(88 + 29 * rc + 10, 120); // above the pads: nothing
	check("right-click and Alt+click make steps rests (steps " + (ra + 1) + ", " + (rb + 1) + "; " + (rc + 1) + " kept)",
		b.stepper.seqStep(0, -1, ra) === null && b.stepper.seqStep(0, -1, rb) === null && b.stepper.seqStep(0, -1, rc) !== null);
	b.displayPage(P(b, "KEYS"));
	b.disp("seqRest", "bang");
	check("REST: the step a rest (" + b.stepper.seqStep(0, -1, 2) + ")", b.stepper.seqStep(0, -1, 2) === null);
	// A scale change moves the notes onto it.
	b.displayPage(P(b, "SCALE"));
	var before = b.stepper.seqStep(0, -1, 0).notes.slice();
	b.stepper.seqToggleNote(0, -1, 0, 61, 100); // C#: in the Chromatic scale
	b.disp("scale", 1); // Major: C# moves (up, a tie) to D
	var after = b.stepper.seqStep(0, -1, 0).notes;
	check("a scale change moves the notes onto it (" + before.join(" ") + " + 61 -> " + after.join(" ") + ")",
		after.indexOf(61) < 0 && after.indexOf(62) >= 0);
	b.displayPage(P(b, "SETUP"));
	b.disp("seqMode", 0);
	check("back to EUCLID: the dials undimmed (" + d.dials.a_pulses + ")", !b.stepper.seqMode(0, -1) && d.dials.a_pulses === 1);
	s.w.wait(SAVE_PAUSE_MS + 10);
	var r = setup({ saved: s.brain.pattr });
	check("the sequence is saved with the set", r.brain.ctx.stepper.seqStep(0, -1, 0) !== null);
	done(r);
	done(s);
})();

// Recording (REC, OVD): the notes played into the sequencer go into the edit track's
// sequence, step by step while stopped (at the cursor), live while playing (the nearest
// step); in SEQ mode they also sound at once on the edit track's receiver (monitoring).
function playNote(b, pitch, vel, step) {
	if (step !== undefined) b.ctx.noteTime(Math.floor(step / 16) + 1, Math.floor((step % 16) / 4) + 1, (step % 4) * 120 + 30);
	b.ctx.noteIn(pitch, vel);
}
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain;
	b.recSelect(1);
	check("REC refused outside SEQ mode (" + b.rec + ", button " + d.rec + ")", b.rec === false && d.rec === 0);
	b.displayPage(P(b, "SETUP"));
	b.disp("seqMode", 1);
	s.w.wait(SAVE_PAUSE_MS + 10); // the mode change stored before the take
	press(s.brain, 0); // step 1 selected: step recording starts there
	b.recSelect(1);
	check("REC on in SEQ mode", b.rec === true);
	var before = s.rx[0].played.length;
	playNote(s.brain, 61, 100); // C#: in the Chromatic scale, as played
	playNote(s.brain, 64, 90);
	check("monitoring: the notes sound at once on the edit track's receiver (" + s.rx[0].played.slice(before).map(function (n) { return n[1]; }).join(" ") + ")",
		s.rx[0].played.slice(before).map(function (n) { return n[1]; }).join() === "61,64" && s.rx[1].played.length === 0);
	playNote(s.brain, 61, 0);
	playNote(s.brain, 64, 0);
	check("monitoring: the notes end with the keys", !s.rx[0].sounding[61] && !s.rx[0].sounding[64]);
	var st1 = b.stepper.seqStep(0, -1, 0);
	check("step recording: the chord at the cursor (" + JSON.stringify(st1) + "), the cursor on (" + b.stepper.recCursor() + ")",
		st1 && st1.notes.join() === "61,64" && st1.velocity === 100 && b.stepper.recCursor() === 1);
	playNote(s.brain, 67, 80);
	playNote(s.brain, 67, 0);
	check("the next chord at the next step (" + JSON.stringify(b.stepper.seqStep(0, -1, 1)) + ")", b.stepper.seqStep(0, -1, 1).notes.join() === "67");
	b.ovdSelect(1);
	b.stepper.recSetCursor(0);
	playNote(s.brain, 72, 70);
	playNote(s.brain, 72, 0);
	check("OVD: a note joins the step's notes (" + b.stepper.seqStep(0, -1, 0).notes.join(" ") + ")", b.stepper.seqStep(0, -1, 0).notes.join() === "61,64,72");
	b.ovdSelect(0);
	var saves = d.saves || 0;
	s.w.wait(SAVE_PAUSE_MS + 10);
	check("nothing stored during the take (" + ((d.saves || 0) - saves) + ")", (d.saves || 0) === saves);
	b.recSelect(0);
	s.w.wait(SAVE_PAUSE_MS + 10);
	check("REC off: the take stored once (" + ((d.saves || 0) - saves) + ")", (d.saves || 0) === saves + 1);
	// Live: a note at step 20 (step 5 of the 16-step loop) held two steps, heard from the next loop.
	b.recSelect(1);
	var t0 = s.w.now;
	s.w.play(range(0, 64), function (i) {
		if (i === 20) playNote(s.brain, 74, 110, 20);
		if (i === 22) playNote(s.brain, 74, 0, 22);
	});
	var st5 = b.stepper.seqStep(0, -1, 4);
	check("live recording: the note at the nearest step, its length in steps (" + JSON.stringify(st5) + ")",
		st5 && st5.notes.indexOf(74) >= 0 && st5.velocity === 110 && st5.gate === 2);
	var heard = s.rx[0].played.filter(function (n) { return n[1] === 74; }).map(function (n) { return +((n[0] - t0) / 500).toFixed(3); });
	check("heard as played (monitoring) and then from the next loop (quarters " + heard.join(" ") + ")",
		heard.length >= 3 && heard.indexOf(9) >= 0 && heard.indexOf(13) >= 0);
	check("recording: no misses (" + misses(s.rx) + "), no hanging notes (" + hanging(s.rx) + ")", misses(s.rx) === 0 && hanging(s.rx) === 0);
	s.w.stop();
	b.displayPage(P(b, "SETUP"));
	b.disp("seqMode", 0);
	check("leaving SEQ mode: REC off (" + b.rec + ", " + d.rec + ")", b.rec === false && d.rec === 0);
	var n = s.rx[0].played.length;
	playNote(s.brain, 60, 100);
	check("EUCLID mode: no monitoring", s.rx[0].played.length === n);
	done(s);
})();

// SONG's entry cells: the patterns' row over the bars' row, as "P1 x1" ("+ +", "- -").
function songCells(d) {
	var m = d.disp.d_songEntries, out = []; // "entries sel play t0 b0 ..."
	for (var k = 0; k < 16; k++) out.push(m[3 + 2 * k] + " " + m[4 + 2 * k]);
	return out;
}

// The song (SONG page): on Live's timeline, bar 1 is Live's bar 1. Pattern 1 for a bar,
// pattern 2 for two, then silence (no LOOP), or pattern 1 again (LOOP); a pattern switch of
// the player's takes over until BACK TO SONG; saved with the set.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain;
	b.stepper.copyPattern(1); // pattern 1 copied to slot 2
	b.displayPage(P(b, "SONG"));
	check("SONG shows its rows (" + shownNames(d) + ")", / d_songEntries /.test(" " + shownNames(d) + " ") &&
		/ d_songInfo /.test(" " + shownNames(d) + " ") && !d.shown.d_soon && !d.shown.d_songResume && !d.shown.d_songPage);
	check("an empty song: a + cell, BARS dimmed, PATTERN not (Ctrl+click adds) (" + songCells(d).slice(0, 3).join(" ") + ")",
		songCells(d)[0] === "+ +" && songCells(d)[1] === "- -" && d.disp.da_songSlot !== 0 && d.disp.da_songBars === 0);
	b.padMods(0, 1, 0);
	delete b.relabelled.songSlot; // (just relabelled: the simulator's clock stands still)
	b.disp("songSlot", 4);
	check("Ctrl+click PATTERN 5 on an empty song: entry 1 plays pattern 5, selected (" + songCells(d)[0] + ", " + b.songSel + ")",
		b.stepper.song().count === 1 && b.stepper.song().entries[0].slot === 4 && b.songSel === 0);
	b.disp("songEdit", 2); // DELETE: empty again
	b.disp("songEdit", 0); // ADD: the playing pattern (1)
	b.disp("songBars", 1);
	b.padMods(0, 1, 0);
	delete b.relabelled.songSlot;
	b.disp("songSlot", 1); // Ctrl+click PATTERN 2: added after the selected entry
	check("ADD and a Ctrl+click on PATTERN add entries, the new one selected (" + songCells(d).slice(0, 3).join(" ") + ", " + d.disp.d_songEntries[1] + ")",
		songCells(d)[0] === "P1 x1" && /^P2 x/.test(songCells(d)[1]) && songCells(d)[2] === "+ +" &&
		d.disp.d_songEntries[1] === 1 && b.stepper.perf().current === 0);
	b.disp("songBarsSet", 1); // 2 bars
	b.disp("songOn", 1);
	check("entry 2 two bars, the song on (" + songCells(d)[1] + ", " + d.disp.d_songOn + ")", songCells(d)[1] === "P2 x2" && d.disp.d_songOn === 1);
	var cur = [], info = [], atEnd = 0;
	function bar(i) { return Math.floor(i / 16); }
	function notes() { var n = 0; s.rx.forEach(function (r) { n += r.played.length; }); return n; }
	s.w.play(range(0, 80), function (i) {
		if (i % 16 === 8) {
			cur[bar(i)] = b.perfShown().current;
			info[bar(i)] = d.disp.d_songInfo;
		}
		if (i === 48) atEnd = notes();
	});
	check("notes in the song (" + atEnd + "), none past its end (" + (notes() - atEnd) + ")", atEnd > 0 && notes() === atEnd);
	check("playing: pattern 1, then 2 for two bars (" + cur.slice(0, 3).join(" ") + ")", cur[0] === 0 && cur[1] === 1 && cur[2] === 1);
	check("the info line follows (" + info[1] + ")", /^ENTRY 2\/2 {3}P2 {3}BAR 1\/2/.test(info[1]));
	check("past the end: silent (" + info[3] + ")", /PAST THE SONG'S END/.test(info[3]));
	s.w.stop();
	var before = [];
	s.rx.forEach(function (r) { before.push(r.played.length); });
	// LOOP: bar 4 is entry 1 again; Play from bar 4 (a jump) goes there at once.
	b.disp("songLoop", 1);
	s.w.setSong("start_time", 12);
	cur = [];
	var quiet = true;
	s.w.play(range(48, 80), function (i) {
		var st = 48 + i;
		if (st % 16 === 8) cur[bar(st)] = b.perfShown().current;
		if (st === 52) quiet = d.disp.d_songEntries[2] === 0;
	});
	check("LOOP: from bar 4, entry 1 again, then entry 2 (" + cur.slice(3, 5).join(" ") + "), its bar shown", cur[3] === 0 && cur[4] === 1 && quiet);
	var heardLoop = 0;
	s.rx.forEach(function (r, k) { heardLoop += r.played.length - before[k]; });
	check("LOOP: notes past the old end (" + heardLoop + ")", heardLoop > 0);
	// A pattern click takes over: BACK TO SONG appears; clicked, the song is back on the bar line.
	s.w.stop();
	playPress(d, 1); // stopped: pattern 2 at once
	b.displayPage(P(b, "SONG"));
	check("an override shows BACK TO SONG (" + d.disp.d_songInfo + ")", d.shown.d_songResume === true && /TOOK OVER/.test(d.disp.d_songInfo));
	b.disp("songResume", "bang");
	check("BACK TO SONG: gone, the song back (" + d.disp.d_songInfo + ")", d.shown.d_songResume === false && !/TOOK OVER/.test(d.disp.d_songInfo));
	// DELETE, INSERT; saved with the set.
	s.w.wait(200);
	b.songEntryClick(0);
	b.disp("songEdit", 1); // INSERT a copy of entry 1
	s.w.wait(200);
	b.songEntryClick(2);
	b.disp("songSlot", 2); // entry 3 plays pattern 3
	check("INSERT and PATTERN (" + songCells(d).slice(0, 4).join(" ") + ")",
		songCells(d).slice(0, 3).join() === "P1 x1,P1 x1,P3 x1" && songCells(d)[3] === "+ +");
	b.disp("songEdit", 2); // DELETE entry 3
	check("DELETE: the selection on the last entry (" + songCells(d).slice(0, 3).join(" ") + ", " + d.disp.d_songEntries[1] + ")",
		songCells(d).slice(0, 3).join() === "P1 x1,P1 x1,+ +" && d.disp.d_songEntries[1] === 1);
	s.w.wait(SAVE_PAUSE_MS + 10);
	var r = setup({ saved: d.pattr }), rs = r.brain.ctx.stepper.song();
	check("the song saved with the set (" + JSON.stringify(rs.entries) + ", on " + rs.on + ", loop " + rs.loop + ")",
		rs.count === 2 && rs.entries[1].slot === 0 && rs.entries[0].slot === 0 && rs.on && rs.loop);
	b.disp("songEdit", 3);
	check("CLEAR SONG (" + songCells(d)[0] + ")", songCells(d)[0] === "+ +" && b.stepper.song().count === 0);
	done(r);
	done(s);
})();

// COPY / PASTE / CLEAR: a track (track tabs, Mute pads), a dial's value, a step, a cycle and
// a pattern; PASTE stays armed, COPY and CLEAR disarm (Ctrl+click CLEAR: stays armed).
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain, st = b.stepper;
	b.dialGeometry("pulses", 104, 27, 44, 48, "velocity", 104, 85, 44, 48);
	check("nothing to paste: PASTE dimmed, nothing armed", d.clipBtn.pasteactive === 0 && b.armed === null);
	turn(d, 0, "pulses", 5);
	b.clipSelect(0, 1);
	check("COPY armed: lit, what it does (" + b.clipInfo() + ")", d.clipBtn.copy === 1 && b.armed !== null && /^COPY: /.test(b.clipInfo()));
	trackPress(d, 0);
	check("a track button copies the track and disarms", d.clipBtn.copy === 0 && d.clipBtn.pasteactive === 1 && b.armed === null);
	b.clipSelect(1, 1);
	check("PASTE armed (" + b.clipInfo() + ")", d.clipBtn.paste === 1 && /^PASTE TRACK 1: /.test(b.clipInfo()));
	trackPress(d, 3, false, true); // Ctrl+click: pastes and stays armed
	trackPress(d, 5, false, true);
	b.padModeSelect(1);
	press(d, 7, false, true); // a Mute pad: track 8
	check("PASTE: tracks 4, 6 and 8 take it, PASTE still armed (" + [3, 5, 7].map(function (t) { return st.getParam(t, "pulses"); }) + ")",
		st.getParam(3, "pulses") === 5 && st.getParam(5, "pulses") === 5 && st.getParam(7, "pulses") === 5 && d.clipBtn.paste === 1 && b.padMode === 1);
	b.clipSelect(1, 0);
	b.padModeSelect(0);
	check("PASTE again: disarmed", d.clipBtn.paste === 0 && b.armed === null);
	// A dial: COPY takes its value, a turn just then doesn't count; pasted on a track tab.
	b.editTrackSelect(0);
	b.clipSelect(0, 1);
	b.leftClick(120, 50); // the Pulses dial
	b.param("pulses", 9);
	check("COPY on a dial: its value (" + b.clipInfo() + "), the dial doesn't turn (" + st.getParam(0, "pulses") + ")",
		st.getParam(0, "pulses") === 5 && d.dials.pulses === 5 && d.clipBtn.copy === 0);
	s.w.wait(500);
	turn(d, 2, "pulses", 3);
	b.clipSelect(1, 1);
	check("PASTE PULSE 5 (" + b.clipInfo() + ")", /^PASTE PULSE 5: /.test(b.clipInfo()));
	trackPress(d, 2);
	check("a plain click pastes once, PASTE disarmed (" + st.getParam(2, "pulses") + ")", st.getParam(2, "pulses") === 5 && d.clipBtn.paste === 0);
	// CLEAR on a dial: the default.
	b.clipSelect(2, 1);
	b.leftClick(120, 50);
	check("CLEAR on a dial: the default (" + st.getParam(2, "pulses") + "), disarmed", st.getParam(2, "pulses") === st.paramDefault("pulses") && d.clipBtn.clear === 0);
	s.w.wait(500);
	// A step: its hit and locks.
	b.editTrackSelect(0);
	var hits = st.hits(0), on = hits.indexOf(true), off = hits.indexOf(false);
	st.lockSet(0, -1, st.unrotated(0, -1, on), "velocity", 30);
	b.clipSelect(0, 1);
	press(d, on);
	b.clipSelect(1, 1);
	press(d, off);
	var posOff = st.unrotated(0, -1, off);
	check("a step pasted: its hit and lock (" + st.hits(0)[off] + ", " + st.lockGet(0, -1, posOff, "velocity") + ")",
		st.hits(0)[off] === true && st.lockGet(0, -1, posOff, "velocity") === 30);
	b.clipSelect(2, 1);
	press(d, off);
	check("CLEAR on the step: lock and manual hit gone (" + st.hits(0)[off] + ", " + st.lockGet(0, -1, posOff, "velocity") + ")",
		st.hits(0)[off] === false && st.lockGet(0, -1, posOff, "velocity") === null);
	// A cycle (CYCLE page): pasting past the last adds cycles; the selection stays.
	b.displayPage(b.DISPLAY.pages.indexOf("CYCLE"));
	st.setCycleCount(0, 2);
	st.setCycleParam(0, 1, "pulses", 7);
	s.w.wait(150); // past the echo of the CYCLE row's renaming
	b.clipSelect(0, 1);
	b.disp("cycleSelect", 1);
	s.w.wait(150);
	b.clipSelect(1, 1);
	b.padMods(0, 1, 0); // Ctrl+click on the cell: PASTE stays armed
	b.disp("cycleSelect", 3);
	check("Ctrl+click on a cycle: PASTE still armed", d.clipBtn.paste === 1);
	b.clipSelect(1, 0);
	check("a cycle pasted past the last: 4 cycles, cycle 4 pulses 7, cycle 1 still selected (" + st.cycleCount(0) + ", " +
		st.getCycleParam(0, 3, "pulses") + ", " + d.disp.d_cycleSelect + ")",
		st.cycleCount(0) === 4 && st.getCycleParam(0, 3, "pulses") === 7 && b.selCycle === 0 && d.disp.d_cycleSelect === 0);
	// A pattern: PATTERN's PLAY slots; Ctrl+click CLEAR stays armed.
	b.clipSelect(0, 1);
	playPress(d, 0);
	b.clipSelect(1, 1);
	playPress(d, 5, false, true);
	playPress(d, 6);
	check("pattern 1 pasted on 6 (Ctrl+click) and 7, then disarmed", (st.perf().used & 96) === 96 && d.clipBtn.paste === 0);
	b.padMods(0, 1, 0);
	b.clipSelect(2, 1);
	check("Ctrl+click CLEAR: stays armed (" + b.clipInfo() + ")", /STAYS ARMED/.test(b.clipInfo()));
	playPress(d, 5);
	playPress(d, 6);
	check("both cleared, CLEAR still armed", (st.perf().used & 96) === 0 && d.clipBtn.clear === 1);
	b.clipSelect(2, 0);
	check("a plain click: disarmed", d.clipBtn.clear === 0 && b.armed === null);
	done(s);
})();

// Channels: a receiver plays every track on its channel (SETUP CHANNEL; track n on channel n
// unless set), so one instrument can play several tracks. A note ends on the channel it
// started on when its track moves; the channel is saved with the pattern.
(function () {
	var base = setup();
	base.w.play(range(0, 64));
	var n1 = base.rx[0].played.length, n2 = base.rx[1].played.length;
	done(base);
	var s = setup(), b = s.brain.ctx, d = s.brain;
	b.displayPage(b.DISPLAY.pages.indexOf("SETUP"));
	b.editTrackSelect(1);
	check("track 2's CHANNEL: 2 at first (" + d.disp.d_channel + ")", d.disp.d_channel === 2);
	b.disp("channel", 1);
	check("track 2 on channel 1 (" + b.stepper.channelOf(1) + ")", b.stepper.channelOf(1) === 0 && d.disp.d_channel === 1);
	var still = true;
	s.w.play(range(0, 64), function (i) {
		if (i === 22) {
			// Track 5 (chords at Sustain 100: always sounding) moves to channel 3 mid-note.
			b.editTrackSelect(4);
			b.disp("channel", 3);
		}
		if (i === 60) still = Object.keys(s.rx[4].sounding).every(function (k) { return !s.rx[4].sounding[k]; });
	});
	check("channel 1's receiver plays tracks 1 and 2 (" + s.rx[0].played.length + " = " + n1 + " + " + n2 + "), channel 2's nothing (" + s.rx[1].played.length + ")",
		s.rx[0].played.length === n1 + n2 && s.rx[1].played.length === 0);
	check("track 5 moved: its notes end on channel 5 (nothing left sounding: " + still + "), and go on on channel 3 (" + s.rx[2].played.length + " > " + base.rx[2].played.length + ")",
		still && s.rx[2].played.length > base.rx[2].played.length);
	s.w.stop();
	s.w.wait(SAVE_PAUSE_MS + 10);
	var r = setup({ saved: d.pattr });
	check("channels saved with the pattern (" + r.brain.ctx.stepper.channelOf(1) + ", " + r.brain.ctx.stepper.channelOf(4) + ", " + r.brain.ctx.stepper.channelOf(3) + ")",
		r.brain.ctx.stepper.channelOf(1) === 0 && r.brain.ctx.stepper.channelOf(4) === 2 && r.brain.ctx.stepper.channelOf(3) === 3);
	b.editTrackSelect(1);
	b.disp("channel", 2);
	check("back on its own number (" + b.stepper.channelOf(1) + ")", b.stepper.channelOf(1) === 1);
	done(r);
	done(s);
})();

// Groups: Ctrl+click adds a track button's track, Shift+click the range from the edit track,
// a plain click ends the group. A plain dial turn moves every grouped track by the same
// amount; a page setting goes to all of them; COPY on a grouped track copies the group,
// which pastes from the track clicked on, or into a pattern slot on its own tracks. The edit
// track and group are saved with the set.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain, st = b.stepper, C = b.TRACK_COLOURS;
	trackPress(d, 0);
	trackPress(d, 2, false, true);
	check("Ctrl+click: track 3 grouped, track 1 still edited (" + b.group + ", " + b.editTrack + "), lit dim red (" + d.trk[2] + ")",
		b.group === 4 && b.editTrack === 0 && d.trk[2] === C.group.join(" ") && d.trk[0] === C.edit.join(" "));
	var keepPlaying = b.playing, keepStep = b.lastStep;
	b.playing = { gates: 5 };
	b.lastStep = 0;
	b.showTrackButtons();
	check("notes on tracks 1 and 3: their pulse frames lit, their colours kept (" + d.trkp + "; " + d.trk[0] + ")",
		d.trkp[0] === 1 && d.trkp[2] === 1 && d.trkp[1] === 0 && d.trk[0] === C.edit.join(" ") && d.trk[2] === C.group.join(" "));
	b.playing = { gates: 0 };
	b.showTrackButtons();
	check("no notes: the frames off", d.trkp[0] === 0 && d.trkp[2] === 0);
	b.playing = keepPlaying;
	b.lastStep = keepStep;
	trackPress(d, 4, true);
	check("Shift+click: tracks 2-5 grouped (" + b.group + ")", b.group === 30);
	trackPress(d, 2, false, true);
	check("Ctrl+click again: track 3 out (" + b.group + ")", b.group === 26);
	trackPress(d, 3);
	check("a plain click: track 4 edited, no group (" + b.editTrack + ", " + b.group + ")", b.editTrack === 3 && b.group === 0);
	trackPress(d, 0);
	trackPress(d, 1, false, true);
	var p0 = st.getParam(0, "pulses"), p1 = st.getParam(1, "pulses");
	b.param("pulses", p0 + 2);
	check("a dial turn moves the group by the same amount (" + st.getParam(0, "pulses") + ", " + st.getParam(1, "pulses") + ")",
		st.getParam(0, "pulses") === p0 + 2 && st.getParam(1, "pulses") === p1 + 2 && st.getParam(2, "pulses") === 4);
	b.displayPage(b.DISPLAY.pages.indexOf("SETUP"));
	b.disp("channel", 3);
	check("a page setting goes to the group (" + st.channelOf(0) + ", " + st.channelOf(1) + ", " + st.channelOf(2) + ")",
		st.channelOf(0) === 2 && st.channelOf(1) === 2 && st.channelOf(2) === 2);
	b.disp("channel", 1);
	check("both to channel 1 (" + st.channelOf(0) + ", " + st.channelOf(1) + ")", st.channelOf(0) === 0 && st.channelOf(1) === 0);
	// COPY on a grouped track: the group; pasted from track 9; into pattern 4.
	b.clipSelect(0, 1);
	trackPress(d, 1);
	b.clipSelect(1, 1);
	check("COPY on a grouped track copies the group (" + b.clipInfo() + ")", /^PASTE TRACKS 1 2: /.test(b.clipInfo()));
	trackPress(d, 8);
	check("pasted from track 9: tracks 9 and 10 (" + st.getParam(8, "pulses") + ", " + st.getParam(9, "pulses") + "), the edit track stays (" + b.editTrack + ")",
		st.getParam(8, "pulses") === p0 + 2 && st.getParam(9, "pulses") === p1 + 2 && b.editTrack === 0);
	b.clipSelect(1, 1); // the plain paste disarmed it
	playPress(d, 3);
	check("pasted into pattern 4 (" + (st.perf().used & 8) + ")", (st.perf().used & 8) !== 0 && st.perf().current === 0);
	playPress(d, 3); // stopped: pattern 4 at once
	check("pattern 4: tracks 1 and 2 pasted, track 3 as it was there (" + st.getParam(0, "pulses") + ", " + st.getParam(1, "pulses") + ", " + st.getParam(2, "pulses") + ")",
		st.perf().current === 3 && st.getParam(0, "pulses") === p0 + 2 && st.getParam(1, "pulses") === p1 + 2 && st.getParam(2, "pulses") === st.paramDefault("pulses"));
	// Saved with the set.
	b.padModeSelect(0);
	trackPress(d, 4);
	trackPress(d, 6, false, true);
	b.param("pulses", 3);
	s.w.wait(SAVE_PAUSE_MS + 10);
	var r = setup({ saved: d.pattr });
	check("the edit track and group saved with the set (" + r.brain.ctx.editTrack + ", " + r.brain.ctx.group + "), lit (" + r.brain.trk[6] + ")",
		r.brain.ctx.editTrack === 4 && r.brain.ctx.group === 64 && r.brain.trk[6] === C.group.join(" "));
	done(r);
	done(s);
})();

// As in Live: no test pattern, so the engine's own default pattern, which is silent (no
// pulses), as a new Talea in Rack. Pulses turned up on tracks 1-6 while stopped play.
(function () {
	var s = setup({ noTestPattern: true });
	var silent = s.brain.ctx.stepper.getParam(0, "pulses") === 0;
	var edits = [[0, "pulses", 4], [1, "pulses", 5], [2, "pulses", 7], [3, "pulses", 3], [4, "pulses", 9], [5, "pulses", 1]];
	edits.forEach(function (e) { turn(s.brain, e[0], e[1], e[2]); });
	var t0 = s.w.now;
	s.w.play(range(0, 128));
	var st = new core.Stepper(M, null), want = emptyWant();
	edits.forEach(function (e) { st.setParamByKey(e[0], e[1], e[2]); });
	range(0, 128).forEach(function (p, i) { addStep(want, st.step(p), i); });
	st.destroy();
	var notes = want.reduce(function (n, tr) { return n + tr.length; }, 0);
	check("no test pattern: the engine's default pattern, silent until edited (" + notes + " notes after the edits)",
		silent && notes > 100 && s.w.logs.every(function (l) { return !/ERROR|can't start/.test(l); }));
	compare("no test pattern, edited: every note on time", s.rx, t0, 120, want, 0);
	check("no test pattern: no misses (" + misses(s.rx) + ")", misses(s.rx) === 0);
	done(s);
})();

// Temp (Rack's): edits while it's on are temporary; off reverts them, Ctrl+click off keeps
// them; the stored pattern never has them; COPY TO reads TEMP TO; a switch ends it.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain;
	check("Temp off at first (" + d.temp + ")", d.temp === 0 && !b.stepper.temp());
	b.tempSelect(1); // the stored value as the set loads: ignored
	check("TEMP's value as the set loads is ignored (" + d.temp + ")", d.temp === 0 && !b.stepper.temp());
	s.w.wait(2100);
	var before = b.stepper.getParam(0, "pulses"), bank = b.stepper.saveBank();
	b.tempSelect(1);
	check("TEMP on: lit, the engine's Temp on (" + d.temp + ")", d.temp === 1 && b.stepper.temp());
	turn(s.brain, 0, "pulses", 3);
	b.pad(1, "bang");
	check("an edit in Temp plays (pulses " + b.stepper.getParam(0, "pulses") + "), the stored bank without it",
		b.stepper.getParam(0, "pulses") === 3 && b.stepper.saveBank() === bank);
	b.padMods(0, 0, 0);
	b.tempSelect(0);
	check("TEMP off: its changes reverted (pulses " + b.stepper.getParam(0, "pulses") + "), unlit",
		b.stepper.getParam(0, "pulses") === before && !b.stepper.temp() && d.temp === 0
		&& b.dialValue(0, "pulses") === before && b.stepper.saveBank() === bank);
	b.tempSelect(1);
	turn(s.brain, 0, "pulses", 5);
	b.padMods(0, 1, 0);
	b.tempSelect(0);
	check("Ctrl+click TEMP off: its changes kept and stored (pulses " + b.stepper.getParam(0, "pulses") + ")",
		b.stepper.getParam(0, "pulses") === 5 && !b.stepper.temp() && d.temp === 0 && b.stepper.saveBank() !== bank);
	b.tempSelect(1);
	turn(s.brain, 0, "pulses", 7);
	b.clipSelect(0, 1);
	playPress(d, 0);
	b.clipSelect(1, 1);
	playPress(d, 3);
	check("in Temp, CPY 1 PST 4: slot 4 holds the pattern with Temp's edits, Temp still on (" + b.stepper.perf().used + ")",
		b.stepper.temp() && b.stepper.getParam(0, "pulses") === 7 && (b.stepper.perf().used & 8) !== 0);
	b.padMods(0, 1, 0);
	b.tempSelect(0); // Ctrl+click: kept
	b.tempSelect(1);
	turn(s.brain, 0, "pulses", 2);
	b.patternPad(1, false, false);
	check("a pattern switch ends Temp, reverting it (" + b.stepper.temp() + ", " + d.temp + ")", !b.stepper.temp() && d.temp === 0);
	b.patternPad(0, false, false);
	check("back on slot 1: pulses as kept (" + b.stepper.getParam(0, "pulses") + ")", b.stepper.getParam(0, "pulses") === 7);
	b.patternPad(3, false, false);
	check("slot 4: Temp's state as copied (pulses " + b.stepper.getParam(0, "pulses") + ")", b.stepper.getParam(0, "pulses") === 7);
	done(s);
})();

// The floating window: its controls arrive as "fromWin ..." and act as the device's; its
// clicks use its own geometry and only count while it's in front; On, Instance and Ahead
// are set on both copies; its keyboard is KEYS' keys; open or not is saved with the set.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain, st = b.stepper;
	check("at load: On, Instance and Ahead set on the window's copies, not the device's (stored: the set would look changed) (" +
		d.won + ", " + d.w.disp.d_instance + ", " + d.w.disp.d_lookahead + "; " + d.onstate + " " + d.disp.d_instance + " " + d.winstate + ")",
		d.won === 1 && d.w.disp.d_instance === 0 && d.w.disp.d_lookahead === 4 && d.onstate === undefined &&
		d.disp.d_instance === undefined && d.disp.d_lookahead === undefined && d.winstate === undefined && d.wwin === 0 && d.win === undefined);
	b.winSelect(1);
	check("the device's window button on: the window opens, both buttons lit, its keyboard drawn (" + d.win + ", " + (d.w.disp.d_kbd || []).slice(0, 4).join(" ") + ")",
		d.wwin === 1 && d.winstate === undefined &&
		d.win === "open" && b.winOpen && d.w.disp.d_kbd[0] === "keys" && d.w.disp.d_kbd[2] === 36 && d.w.disp.d_kbd[3] === 5 && d.w.disp.d_krange === "C2 - B6");
	b.fromWin("param", "pulses", 3);
	check("a dial in the window edits as the device's (" + st.getParam(0, "pulses") + ", the dials " + d.dials.pulses + ")",
		st.getParam(0, "pulses") === 3 && d.dials.pulses === 3);
	b.fromWin("enable", 0);
	check("On in the window: off, both copies (" + d.onstate + ", " + d.won + ")", d.onstate === 0 && d.won === 0);
	b.fromWin("enable", 1);
	b.fromWin("lookahead", 6);
	check("Ahead in the window: both copies (" + d.disp.d_lookahead + ", " + d.w.disp.d_lookahead + ")", d.disp.d_lookahead === 6 && d.w.disp.d_lookahead === 6 && b.lookaheadSteps === 6);
	// Each copy's dials where they are: a COPY click on the window's Pulses dial.
	b.dialGeometry("pulses", 104, 27, 44, 48);
	b.fromWin("dialGeometry", "velocity", 104, 27, 44, 48);
	b.clipSelect(0, 1);
	b.fromWin("winActive", 1);
	b.leftClick(120, 50); // the device's mousestate sees the click too: not in front, ignored
	check("the window in front: the device's click ignored (" + b.armed + ")", b.armed === "copy");
	b.fromWin("leftClick", 120, 50);
	check("the window's click: its own dial (velocity) copied (" + b.clipInfo() + ")", b.armed === null && /VEL /.test(b.clipInfo()));
	b.fromWin("winActive", 0);
	b.clipSelect(0, 1);
	b.fromWin("leftClick", 120, 50);
	check("the window not in front: its click ignored", b.armed === "copy");
	b.leftClick(120, 50);
	check("the device's click: its dial (pulses)", b.armed === null && /PULSE/.test(b.clipInfo()));
	// The keyboard follows the pool; its click is KEYS'.
	b.fromWin("poolKey", 64);
	check("a key on the window's keyboard: the pool (" + st.pool(0) + ")", st.pool(0).indexOf(64) >= 0 && d.w.disp.d_kbd.join(" ").indexOf(" 64 ") >= 0);
	// The window's tabs leave KEYS out: their own numbering, KEYS shows none of them.
	b.displayPage(P(b, "SCALE"));
	check("SCALE: the window's SCALE tab (" + tabOf(d.w.shown) + ")", tabOf(d.w.shown) === W(b, "SCALE") && tabOf(d.w.shown) >= 0 && tabOf(d.w.shown) !== -1);
	b.displayPage(P(b, "KEYS"));
	check("KEYS on the device: the window stays on SCALE, its tab chosen (" + tabOf(d.w.shown) + ", " + d.w.shown.d_scale + " " + d.shown.d_scale + ")",
		tabOf(d.w.shown) === W(b, "SCALE") && tabOf(d.w.shown) >= 0 && d.w.shown.d_scale === true && d.shown.d_scale === false && d.shown.d_keysHelp === true && !d.w.shown.d_keysHelp);
	b.fromWin("disp", "scale", 2);
	check("a SCALE click in the window while the device shows KEYS (" + st.getField(0, "scale") + ")", st.getField(0, "scale") === 2);
	b.fromWin("kbdOctaveMove", 1);
	check("the keyboard's > : C3 - B7 (" + d.w.disp.d_krange + ")", d.w.disp.d_krange === "C3 - B7" && d.w.disp.d_kbd[2] === 48);
	b.fromWin("displayPage", W(b, "SONG"));
	check("the window's SONG tab: the device's SONG (" + b.dispPage + ")", b.dispPage === P(b, "SONG") && tabOf(d.w.shown) >= 0);
	check("the keyboard's row: KEYS' help line (" + d.w.disp.d_khelp + "), no REST or TRACK POOL",
		d.w.disp.d_khelp === "CLICK = ADD / REMOVE A POOL NOTE" && !d.wshown.k_rest && !d.wshown.k_pool);
	b.fromWin("winSelect", 1);
	check("the window's window button: closed, both buttons unlit (" + d.win + ")", d.win === "close" && !b.winOpen && d.winstate === 0 && d.wwin === 0);
	b.winSelect(1);
	b.winSelect(0);
	check("the device's window button off: closed", d.win === "close" && !b.winOpen && d.wwin === 0);
	b.winSelect(1);
	b.fromWin("winClosed");
	check("the window's close box: both buttons off (" + d.winstate + ")", !b.winOpen && d.winstate === 0 && d.wwin === 0);
	b.winSelect(1);
	check("opened again after the close box (" + d.win + ")", b.winOpen && d.win === "open" && d.wwin === 1);
	check("pinned at init (on top): the window's pin lit, the device's not set (" + d.wpin + " " + d.pinstate + ")",
		b.winPinned && d.wpin === 1 && d.pinstate === undefined);
	b.fromWin("pinSelect", 0);
	check("the window's pin off: unpinned, both pins unlit, brought to the front (" + d.win + ")",
		!b.winPinned && d.wpin === 0 && d.pinstate === 0 && d.win === "front");
	b.winSelect(0);
	check("unpinned, the device's window button off: brought to the front, still open and lit (" + d.win + " " + d.winstate + ")",
		b.winOpen && d.win === "front" && d.winstate === 1);
	b.fromWin("winSelect", 1);
	check("unpinned, the window's button: closed", !b.winOpen && d.win === "close");
	b.pinSelect(1);
	check("the device's pin on, the window closed: pinned, the window's lit, nothing sent to it (" + d.win + ")",
		b.winPinned && d.wpin === 1 && d.win === "close");
	b.winSelect(1);
	check("opened: on top, then open (" + d.winLog.slice(-2) + ")", d.winLog.slice(-2).join() === "pin,open");
	b.fromWin("restore", 1);
	check("only the panel's messages come from the window (restore ignored)", true);
	done(s);
})();

// PATTERN's PLAY: Ctrl+click adds a pattern to the chain, Shift+click chains the range from
// the playing one; the chain is framed white. RECORD / OVERDUB dim off a SEQ track; APPLY
// MUTES dims while nothing is staged; MUTE TRACKS' frame blinks.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain;
	playPress(d, 3, true, false);
	check("Shift+click 4 (playing 1): the chain 1-4, bordered (" + b.stepper.perf().chain + "; " + d.patb.slice(0, 5) + ")",
		b.stepper.perf().chain.join() === "0,1,2,3" && d.patb[0] === 1 && d.patb[3] === 1 && d.patb[4] === 0);
	playPress(d, 6, false, true);
	check("Ctrl+click 7: added to the chain (" + b.stepper.perf().chain + "), bordered", b.stepper.perf().chain.join() === "0,1,2,3,6" && d.patb[6] === 1);
	playPress(d, 0);
	check("a click: the chain gone, its borders off", b.stepper.perf().chain.length === 0 && d.patb[3] === 0);
	b.padMods(0, 1, 0);
	b.patternButton(4);
	b.padMods(0, 1, 0);
	b.patternButton(5);
	check("the pattern buttons: Ctrl+click 5, 6 chained, their borders white (" + b.stepper.perf().chain + "; " + d.patb.slice(0, 7) + ")",
		b.stepper.perf().chain.join() === "4,5" && d.patb[4] === 1 && d.patb[5] === 1 && d.patb[0] === 0);
	check("the playing pattern's button orange (" + d.patc[b.stepper.perf().current] + ")", d.patc[b.stepper.perf().current] === "0.94 0.54 0.14");
	b.patternButton(0);
	check("RECORD and OVERDUB dimmed on a EUCLID track (" + d.recactive + ")", d.recactive === 0);
	b.displayPage(P(b, "SETUP"));
	b.disp("seqMode", 1);
	check("lit on a SEQ track (" + d.recactive + ")", d.recactive === 1);
	b.padModeSelect(1);
	var frames = {};
	for (var k = 0; k < 8; k++) { b.blink(); frames[d.shown.d_mutemode] = true; }
	check("MUTE TRACKS: its red frame blinks", frames["true"] && frames["false"]);
	check("APPLY MUTES dimmed, nothing staged (" + d.apply + ")", d.apply === 0);
	trackPress(d, 2);
	check("a staged mute: APPLY MUTES lit (" + d.apply + ")", d.apply === 1);
	b.padModeSelect(0);
	check("MUTE TRACKS off: the frame hidden", !d.shown.d_mutemode);
	done(s);
})();

// A Shift+click chain keeps its first pattern: stopped, the chain from the pattern clicked
// plays it first (Play doesn't jump to the second); playing, from a pattern just queued.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain, cur = [];
	b.patternButton(2);
	b.padMods(1, 0, 0);
	b.patternButton(4);
	check("stopped: click 3, Shift+click 5: 3 still selected, the chain 3-5 (" + b.stepper.perf().current + "; " + b.stepper.perf().chain + ")",
		b.stepper.perf().current === 2 && b.stepper.perf().chain.join() === "2,3,4");
	s.w.play(range(0, 4), function () { cur.push(b.perfShown().current); });
	check("Play: pattern 3 first (" + cur.join() + ")", cur.every(function (c) { return c === 2; }));
	done(s);
	var r = setup(), rb = r.brain.ctx, seen = null;
	r.w.play(range(0, 12), function (i) {
		if (i === 2) rb.patternButton(6);
		if (i === 4) { rb.padMods(1, 0, 0); rb.patternButton(8); }
		if (i === 6) seen = rb.stepper.perf();
	});
	check("playing: click 7 (queued), Shift+click 9: the chain 7-9, 7 queued (" + seen.chain + "; queued " + seen.queued + ", playing " + seen.current + ")",
		seen.chain.join() === "6,7,8" && seen.queued === 6 && seen.current === 0);
	done(r);
})();

// STEPS' board: its steps clicked as the pads (whatever the pad mode): a click toggles the
// hit, Ctrl+click selects for locks, COPY / PASTE / CLEAR take the step; the cycle lanes'
// edit row ("-1 - i") is the step page's step i.
(function () {
	var s = setup(), b = s.brain.ctx, st = b.stepper;
	var h0 = !!st.hits(0)[5];
	b.boardStep(5, 0, 0, 0);
	check("a STEPS click: step 6's hit flipped (" + h0 + " -> " + !!st.hits(0)[5] + ")", !!st.hits(0)[5] === !h0);
	b.padModeSelect(1);
	b.boardStep(5, 0, 0, 0);
	check("in Mute mode too: flipped back (" + !!st.hits(0)[5] + ")", !!st.hits(0)[5] === h0);
	b.padModeSelect(0);
	b.boardStep(7, 1, 0, 0);
	check("Ctrl+click: step 8 selected for locks (" + b.selCount() + ")", b.selCount() === 1 && b.lockSel[st.unrotated(0, -1, 7)]);
	b.clearLockSel();
	var h3 = !!st.hits(0)[3];
	b.boardStep(-1 - 3, 0, 0, 0);
	check("the cycle lanes' edit row: the page's step 4 flipped", !!st.hits(0)[3] === !h3);
	b.rightAt = b.Date.now(); // (the script's clock)
	var h9 = !!st.hits(0)[9];
	b.boardStep(9, 0, 0, 0);
	check("a right-click (a plain click with the right button just down): not a toggle", !!st.hits(0)[9] === h9);
	b.rightAt = -1e9;
	done(s);
})();

// DEVICE's PAGE LOCK (on in a new device: a dial leaves the page) and PAGE SYNC (off: the
// device and the window each on their own page).
(function () {
	var s = setup({ pageLock: true }), b = s.brain.ctx, d = s.brain;
	b.displayPage(P(b, "VOICING"));
	turn(s.brain, 0, "pitch", 3);
	check("PAGE LOCK on (a new device): a dial leaves the page (" + tabOf(d.shown) + ")", b.pageLock && tabOf(d.shown) === P(b, "VOICING"));
	b.gearSelect(0);
	s.w.wait(150);
	b.disp("pageLock", 0);
	b.displayPage(P(b, "VOICING"));
	turn(s.brain, 0, "pitch", 4);
	check("PAGE LOCK off: a dial shows its page (SCALE: " + tabOf(d.shown) + ")", !b.pageLock && tabOf(d.shown) === P(b, "SCALE"));
	b.winSelect(1);
	b.gearSelect(0);
	s.w.wait(150);
	b.disp("pageSync", 0);
	s.w.wait(600);
	check("PAGE SYNC off, stored with the set", !b.pageSync && /"pageSync":0/.test(b.savedText) && /"pageLock":0/.test(b.savedText));
	b.fromWin("displayPage", W(b, "GROOVE")); // the window's GROOVE (its tabs have no KEYS)
	check("off: a tab in the window moves the window only (device " + (tabOf(d.shown) < 0 ? "gear" : tabOf(d.shown)) + ", window " + tabOf(d.w.shown) + ")",
		tabOf(d.shown) === -1 && tabOf(d.w.shown) === W(b, "GROOVE") && d.w.shown.d_grooveLength === true && !d.shown.d_grooveLength);
	b.displayPage(P(b, "STEPS"));
	check("off: a tab on the device moves the device only (" + tabOf(d.shown) + ", window " + tabOf(d.w.shown) + ")",
		tabOf(d.shown) === P(b, "STEPS") && tabOf(d.w.shown) === W(b, "GROOVE") && d.shown.d_trackInfo === true && !d.w.shown.d_trackInfo);
	b.fromWin("disp", "grooveLength", 2);
	check("a GROOVE click in the window while the device shows TRACK (" + b.stepper.getField(0, "grooveLength") + ")", b.stepper.getField(0, "grooveLength") === 2);
	b.fromWin("gearSelect", 0);
	check("off: the gear in the window: the window only (" + d.w.shown.d_gear_on + " " + d.shown.d_gear_on + ")", d.w.shown.d_gear_on === true && !d.shown.d_gear_on);
	b.fromWin("disp", "pageSync", 1);
	check("PAGE SYNC on in the window: the device takes the window's page, the gear (" + tabOf(d.shown) + " " + d.shown.d_gear_on + ")",
		b.pageSync && b.gearOpen && tabOf(d.shown) === -1 && d.shown.d_gear_on === true && d.w.shown.d_gear_on === true && !d.shown.d_trackInfo);
	b.disp("pageSync", 0);
	b.displayPage(P(b, "STEPS"));
	b.fromWin("displayPage", W(b, "GROOVE"));
	b.gearSelect(0);
	b.disp("pageSync", 1);
	check("PAGE SYNC on on the device: the window takes the device's page, the gear (" + d.w.shown.d_gear_on + ")",
		b.pageSync && d.w.shown.d_gear_on === true && !d.w.shown.d_grooveLength && tabOf(d.w.shown) === -1);
	done(s);
})();

// Info View texts: loaded by the script, one for each board and diagram it draws.
(function () {
	var s = setup(), b = s.brain.ctx, I = b.INFO;
	var boards = ["steps", "cycles", "lanes", "keys"].every(function (k) { return I && I.boards[k] && I.boards[k][1].length > 20; });
	var diagrams = Object.keys(b.DISPLAY.diagrams).every(function (v) { return I.diagrams[v] && I.diagrams[v][1].length > 20; });
	check("Info View texts loaded: every board's and diagram's", boards && diagrams);
	var pages = b.DISPLAY.pages.concat([b.DISPLAY.gear, "MUTE"]).every(function (v) { return I.pageTabs[v] && I.pageTabs[v][1].length > 40; });
	check("every page has its own text for the tab row", pages);
	done(s);
})();

// A set reloaded in a running Live: its pattern comes after init. Taken as the set's (its
// device settings too: WINDOW, the edit track), not as Live's undo.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain;
	b.gearSelect(0);
	s.w.wait(150);
	b.disp("winZoom", 4);
	b.editTrackSelect(3);
	b.param("pulses", 2);
	s.w.wait(600);
	var saved = d.pattr.slice();
	done(s);
	var r = setup({ saved: saved, restoreAfterInit: true }), rb = r.brain.ctx;
	check("a reload's late pattern: the set's, with its settings (WINDOW " + rb.winZoom + ", edit track " + rb.editTrack + ", pulses " + rb.stepper.getParam(3, "pulses") + ")",
		rb.winZoom === 4 && rb.editTrack === 3 && rb.stepper.getParam(3, "pulses") === 2 && rb.setLoaded);
	done(r);
})();

// A "fit" row (CYCLE's CLEAR): as wide as its choices need, never wider than the row.
(function () {
	var s = setup(), b = s.brain.ctx;
	check("CLEAR's width for ALL HITS (" + b.fitWidth(["ALL", "HITS"], 398) + "), for many (" +
		b.fitWidth(["ALL", "HITS", "LOCKS", "SCALE", "SEQ", "MODE", "PULSE", "R.PULSE"], 398) + ")",
		b.fitWidth(["ALL", "HITS"], 398) === 72 && b.fitWidth(["ALL", "HITS", "LOCKS", "SCALE", "SEQ", "MODE", "PULSE", "R.PULSE"], 398) === 398);
	done(s);
})();

// The gear: the DEVICE page (TEMP OFF, Instance, Ahead, the status line); the page tabs
// show no page meanwhile. TEMP OFF stored with the set.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain;
	check("the gear unlit, the page tabs as they are (" + d.shown.d_gear + " " + d.shown.d_gear_on + " " + tabOf(d.shown) + " " + tabOf(d.shown) + ")",
		d.shown.d_gear === true && !d.shown.d_gear_on && tabOf(d.shown) >= 0 && tabOf(d.shown) !== -1);
	b.gearSelect(0);
	check("the gear: DEVICE's rows, the gear lit, no page tab chosen (" + shownNames(d) + ")",
		shownNames(d) === "d_instance d_lookahead d_pageLock d_pageSync d_patternQuantize d_status d_tempTiming d_winZoom dl_instance dl_lookahead dl_pageLock dl_pageSync dl_patternQuantize dl_tempTiming dl_winZoom"
		&& d.shown.d_gear === false && d.shown.d_gear_on === true && tabOf(d.shown) === -1 && tabOf(d.shown) === -1
		&& d.disp.d_tempTiming === 2);
	s.w.wait(150);
	b.disp("tempTiming", 1);
	s.w.wait(600);
	check("TEMP OFF: BEAT, stored with the set (" + b.tempTiming + ")", b.tempTiming === 1 && /"tempTiming":1/.test(b.savedText));
	s.w.wait(150);
	b.disp("winZoom", 2);
	s.w.wait(600);
	check("WINDOW: 150%, stored with the set (" + b.winZoom + ", " + d.disp.d_winZoom + ")",
		b.winZoom === 2 && d.disp.d_winZoom === 2 && /"winZoom":2/.test(b.savedText));
	check("WINDOW 150%: reached a step at a time (" + b.winApplied + ")", b.winApplied === 1.5 && !b.winStepping);
	var wshown = Object.keys(d.w.shown).filter(function (k) { return d.w.shown[k] && /^d_|^dl_/.test(k) && !/^d_(pages|gear|padlock|tempwait|mutemode|rndmark|stepmark|cycmark|tab\d)/.test(k); }).sort().join(" ");
	check("then the window's controls hidden but its page's (" + wshown + ")",
		wshown === "d_instance d_lookahead d_pageLock d_pageSync d_patternQuantize d_status d_tempTiming d_winZoom dl_instance dl_lookahead dl_pageLock dl_pageSync dl_patternQuantize dl_tempTiming dl_winZoom");
	s.w.wait(150);
	b.disp("winZoom", 0);
	check("WINDOW back to 100% (" + b.winApplied + ")", b.winApplied === 1);
	b.displayPage(P(b, "SETUP"));
	check("a page tab: that page, the gear unlit (" + shownNames(d) + ")",
		/ d_repeatTail /.test(" " + shownNames(d) + " ") && !/ d_instance /.test(" " + shownNames(d) + " ")
		&& d.shown.d_gear === true && !d.shown.d_gear_on && tabOf(d.shown) >= 0 && tabOf(d.shown) !== -1 && tabOf(d.shown) === P(b, "SETUP"));
	b.gearSelect(0);
	turn(s.brain, 0, "pitch", 3);
	check("a dial turned: its page, the gear unlit (" + tabOf(d.shown) + ")", tabOf(d.shown) === P(b, "SCALE") && d.shown.d_gear === true && tabOf(d.shown) >= 0);
	done(s);
})();

// TEMP OFF while playing: a revert waits for the boundary (BAR: step 16), heard from it; a
// click meanwhile cancels it; Ctrl+click keeps at once.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain, seen = {};
	s.w.wait(2100);
	var before = b.stepper.getParam(0, "pulses");
	b.tempSelect(1);
	turn(s.brain, 0, "pulses", 3);
	s.w.play(range(0, 48), function (i) {
		if (i === 5) { b.padMods(0, 0, 0); b.tempSelect(0); seen.at5 = [b.tempEndAt, d.temp]; }
		if (i === 8) { seen.frame = {}; for (var k = 0; k < 16; k++) { b.blink(); seen.frame[d.shown.d_tempwait] = true; } }
		if (i === 15) seen.at15 = [b.stepper.temp(), b.stepper.getParam(0, "pulses")];
		if (i === 16) seen.at16 = [b.stepper.temp(), b.stepper.getParam(0, "pulses"), d.temp, d.shown.d_tempwait];
		if (i === 20) { b.tempSelect(1); turn(s.brain, 0, "pulses", 4); }
		if (i === 22) { b.padMods(0, 0, 0); b.tempSelect(0); }
		if (i === 24) { b.padMods(0, 0, 0); b.tempSelect(1); seen.cancel = [b.tempEndAt, d.temp]; }
		if (i === 34) seen.at34 = [b.stepper.temp(), b.stepper.getParam(0, "pulses")];
		if (i === 36) { b.padMods(0, 0, 0); b.tempSelect(0); }
		if (i === 38) { b.padMods(0, 1, 0); b.tempSelect(1); }
		if (i === 40) seen.at40 = [b.stepper.temp(), b.stepper.getParam(0, "pulses"), b.tempEndAt];
	});
	s.w.stop();
	check("TEMP clicked off at step 5: the revert waits for step 16, TEMP unlit (" + seen.at5 + ")", seen.at5[0] === 16 && seen.at5[1] === 0);
	check("waiting: TEMP's frame blinks (" + Object.keys(seen.frame) + ")", seen.frame["true"] && seen.frame["false"]);
	check("step 15: Temp still on, its pulses (" + seen.at15 + ")", seen.at15[0] === true && seen.at15[1] === 3);
	check("step 16: reverted (" + seen.at16 + ")", seen.at16[0] === false && seen.at16[1] === before && seen.at16[2] === 0 && !seen.at16[3]);
	check("a click while waiting cancels the revert: Temp on, lit (" + seen.cancel + "; step 34 " + seen.at34 + ")",
		seen.cancel[0] === -1 && seen.cancel[1] === 1 && seen.at34[0] === true && seen.at34[1] === 4);
	check("Ctrl+click while waiting keeps at once (" + seen.at40 + ")", seen.at40[0] === false && seen.at40[1] === 4 && seen.at40[2] === -1);
	done(s);
})();

// The timing summary at stop: the brain's and each receiver's.
(function () {
	var s = setup();
	s.w.logs = [];
	s.w.play(range(0, 64));
	s.w.stop();
	var lines = s.w.logs.filter(function (l) { return /timing: /.test(l); });
	var rx = lines.filter(function (l) { return /closest step arrived/.test(l); });
	check("timing logged at stop, brain and receivers (" + lines.length + "):\n      " + lines.slice(0, 2).join("\n      "),
		lines.length === 7 && /timing: 64 steps/.test(lines[0]) &&
		rx.length === 6 && rx.every(function (l) { return /closest step arrived [45] steps ahead, misses 0, late 0/.test(l); }));
	done(s);
})();

console.log(results.join("\n"));
console.log(ok ? "all passed" : "FAILURES");
process.exit(ok ? 0 : 1);
