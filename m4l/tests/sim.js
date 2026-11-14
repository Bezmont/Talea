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
			else if (n === 5 && role === "brain") {
				// the pads, the step page tabs and the Follow button
				if (a[0] === "pad") (d.pads = d.pads || [])[a[1]] = a.slice(2, 5).join(" ");
				else if (a[0] === "page") d.page = a[1];
				else if (a[0] === "follow") d.follow = a[1];
				else if (a[0] === "mode") d.mode = a[1];
				else if (a[0] === "apply") d.apply = a[1];
			} else if (n === 6 && role === "brain") {
				// the display: [thispatcher] shows and hides; the rest sets controls
				d.shown = d.shown || {};
				d.disp = d.disp || {};
				if (a[0] === "script") d.shown[a[2]] = a[1] === "show";
				else if (a[0] === "d_diagram" && a[1] === "play") d.play = a[2];
				else if (a[0] === "d_diagram") d.diagram = a.slice(1);
				else d.disp[a[0]] = a.length > 2 || /^dr_/.test(a[0]) ? a.slice(1) : a[1];
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
	if (role === "receiver") ctx.trackSelect(trackNo);
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
	b.pad(3, 1);         // step 20
	var after = b.stepper.hits(1);
	check("pad on page 2 toggles step 20 (" + before[19] + " -> " + after[19] + "), and only it",
		after[19] === !before[19] && after.every(function (h, i) { return i === 19 || h === before[i]; }));
	check("choosing a page turns Follow off (" + s.brain.follow + "), page shown " + s.brain.page,
		s.brain.follow === 0 && s.brain.page === 1);
	b.stepPageSelect(3); // past the track's 32 steps: stays on page 2
	check("a page past the track's Steps doesn't take (page " + s.brain.page + ")", s.brain.page === 1);
	var hitColour = "0.94 0.54 0.14", ok = true;
	for (var i = 0; i < 16; i++) ok = ok && (s.brain.pads[i] === hitColour) === after[16 + i];
	check("the pads show the page's hits", ok);
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
		if (i > 2) {
			var lit = (s.brain.pads || []).filter(function (c) { return c === "1 1 1" || c === "0.35 0.35 0.35"; }).length;
			if (lit === 1) seen++;
			pages[s.brain.page] = true;
		}
	});
	check("one pad lit for the playing step on each tick (" + seen + " of 93)", seen === 93);
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

// A pad press as the patch sends it: the modifier keys first, then the press.
function press(b, i, shift, ctrl) {
	b.ctx.padMods(shift ? 1 : 0, ctrl ? 1 : 0);
	b.ctx.pad(i, "bang");
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
		if (i === 14) shown.staged = s.brain.pads[2];
		if (i === 16) b.applyMutes();
		if (i === 30) press(s.brain, 4, false, true);
		if (i === 40) shown.muted = [s.brain.pads[0], s.brain.pads[2], s.brain.pads[4], s.brain.pads[1]];
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
	check("the pads show the mutes (" + JSON.stringify(shown) + ")",
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
		if (i === 6) { b.padModeSelect(2); press(s.brain, 1); }
		if (i === 8) seen.queued = b.stepper.perf().queued;
		if (i === 150) seen.before = b.stepper.getParam(0, "pulses");
		if (i === 150) press(s.brain, 0, false, true);
		if (i === 160) seen.after = [b.stepper.perf().current, b.stepper.getParam(0, "pulses"), s.brain.pads[0], s.brain.pads[1]];
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
		seen.queued === 1 && seen.before === 13 && seen.after[0] === 0 && seen.after[1] === 9 && seen.after[2] === "0.94 0.54 0.14" && seen.after[3] === "0.24 0.14 0.04");
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
	b.padModeSelect(2);
	press(s.brain, 1);
	check("stopped: pattern 2 at once (" + b.stepper.perf().current + ")", b.stepper.perf().current === 1);
	press(s.brain, 0, true);
	press(s.brain, 1, true);
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
		if (i === 10) { b.padModeSelect(2); press(s.brain, 1, false, true); }
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

function shownNames(d) {
	return Object.keys(d.shown).filter(function (k) { return d.shown[k]; }).sort().join(" ");
}

// Pages: a page shows its rows (or the note for one not built yet), set from the edit
// track; an edit while stopped is heard at Play.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain;
	check("at load the TRACK page: its info line and steps (" + shownNames(d) + ")", shownNames(d) === "d_board d_trackInfo");
	b.displayPage(4); // SCALE
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
	b.displayPage(6); // VOICING
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
	b.displayPage(7);
	check("GROOVE: BARS dimmed for AGOGO (" + d.disp.da_grooveLength + ")", d.disp.da_grooveLength === 0);
	b.disp("groove", 4);
	check("GROOVE: BARS active for SAW (" + d.disp.da_grooveLength + ")", d.disp.da_grooveLength === 1);
	b.disp("grooveLoop", 1);
	check("GROOVE: SYNC LOOP relabels BARS as LOOPS (" + d.disp.dl_grooveLength + ")", d.disp.dl_grooveLength === "LOOPS");
	b.displayPage(5);
	b.disp("phrase", 8);
	check("PHRASE: USER is the engine's 9, shown as the 9th choice (" + b.stepper.getField(0, "phrase") + ", " + d.disp.d_phrase + ")",
		b.stepper.getField(0, "phrase") === 9 && d.disp.d_phrase === 8);
	done(s);
})();

// Mute mode shows its settings over the page; APPLY at BAR lands staged mutes on the bar
// line; a page tab leaves Mute mode, back to the mode before it.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain, steps = range(0, 64), t0 = s.w.now;
	b.displayPage(4);
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
	b.displayPage(9);
	check("a page tab leaves Mute mode (mode " + d.mode + "), PATTERN shown (" + shownNames(d) + ")",
		d.mode === 0 && shownNames(d) === "d_patternClear d_patternCopy d_patternPlay d_patternQuantize dl_patternClear dl_patternCopy dl_patternPlay dl_patternQuantize");
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
	check("a Repeats dial shows SETUP, where Choke / Tail is (" + d.disp.d_pages + ", " + shownNames(d) + ")",
		d.disp.d_pages === 11 && / d_repeatTail /.test(" " + shownNames(d) + " ") && / d_instance /.test(" " + shownNames(d) + " "));
	turn(s.brain, 0, "pitch", 3);
	check("Pitch shows SCALE (" + d.disp.d_pages + ")", d.disp.d_pages === 4);
	b.padModeSelect(1);
	turn(s.brain, 0, "voicing", 2);
	check("in Mute mode the Mute view stays (" + shownNames(d) + "), the page moves under it (" + d.disp.d_pages + ")",
		shownNames(d) === "d_muteCuts d_muteTiming dl_muteCuts dl_muteTiming" && d.disp.d_pages === 6);
	b.padModeSelect(0);
	check("leaving Mute mode: VOICING (" + shownNames(d) + ")", /d_style/.test(shownNames(d)));
	done(s);
})();

// PATTERN: COPY TO and CLEAR; PLAY queues and shows the playing pattern.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain;
	b.displayPage(9);
	s.w.wait(200); // a click comes a while after the page shows (and renames its choices)
	b.disp("patternCopy", 3);
	check("COPY TO 4: slot 4 holds the pattern (" + b.stepper.perf().used + ")", (b.stepper.perf().used & 8) !== 0);
	b.disp("patternCopy", 5); // at once: the echo of the rename COPY TO 4 caused
	check("an echo just after a rename is ignored (slot 6 " + ((b.stepper.perf().used & 32) ? "copied" : "untouched") + ")",
		(b.stepper.perf().used & 32) === 0);
	s.w.wait(200);
	b.disp("patternPlay", 3);
	check("PLAY 4 while stopped: playing at once, shown (" + b.stepper.perf().current + ", " + d.disp.d_patternPlay + ")",
		b.stepper.perf().current === 3 && d.disp.d_patternPlay === 3 && b.stepper.getParam(0, "pulses") === 9);
	check("filled slots marked in PLAY, COPY TO and CLEAR (" + d.disp.dr_patternPlay.slice(0, 5).join(" ") + ")",
		d.disp.dr_patternPlay[0] === "1*" && d.disp.dr_patternPlay[3] === "4*" && d.disp.dr_patternPlay[1] === "2" &&
		d.disp.dr_patternCopy[3] === "4*" && d.disp.dr_patternClear[0] === "1*");
	s.w.wait(200);
	b.disp("patternClear", 3);
	check("CLEAR 4: the playing pattern back to the default (pulses " + b.stepper.getParam(0, "pulses") + "), dials follow",
		b.stepper.getParam(0, "pulses") === 0 && (b.stepper.perf().used & 8) === 0);
	check("cleared slot unmarked (" + d.disp.dr_patternPlay[3] + ")", d.disp.dr_patternPlay[3] === "4");
	done(s);
})();

// The diagrams: GROOVE's accent shape with a playhead, PHRASE's notes or its User grid
// (edited by clicking), VOICING's next notes.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain;
	b.displayPage(7);
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
	b.displayPage(6);
	check("VOICING: the next 16 notes, as dashes (" + d.diagram.slice(0, 3).join(" ") + ")",
		d.diagram[0] === "notes" && d.diagram[1] === "NEXT 16 NOTES" && d.diagram[2] === 1 && d.diagram.length === 3 + 16);
	b.displayPage(5);
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
	var curve = []; for (var i = 0; i <= 128; i++) curve.push(Math.sin(i / 20));
	ctx.groove.apply(null, ["2 BARS", 0.5].concat(curve));
	ctx.messagename = "play"; ctx.anything(0.25);
	ctx.messagename = "live_lcd_control_fg"; ctx.anything(1, 0.5, 0, 1);
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
	ctx.messagename = "steps";
	ctx.anything.apply(null, [20, 16, 3, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0]);
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
	b.displayPage(3);
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
	b.displayPage(4);
	b.disp("scale", 1); // Major
	b.scaleKey(1);      // C#: into the scale, which becomes User
	check("SCALE: a note click makes the User scale (" + b.stepper.getField(0, "scale") + ", notes " + b.stepper.scaleNotes(0).toString(2) + ")",
		b.stepper.getField(0, "scale") === 23 && (b.stepper.scaleNotes(0) & 2) !== 0 && d.disp.d_scale === 23 && d.diagram[0] === "scalekeys");
	done(s);
})();

// RANDOM: the amounts and their per cycle / per tick switches, Variance, Rate, Slew, Lock
// and the sequence buttons, for the edit track; heard from the next step while playing,
// saved with the set.
(function () {
	var s = setup(), b = s.brain.ctx, d = s.brain;
	b.displayPage(8);
	var shown = shownNames(d).split(" ");
	check("RANDOM shows 15 amounts, their switches and the rest (" + shown.length + " controls)",
		shown.length === 15 * 3 + 4 * 2 + 2 && shown.indexOf("d_rnd_pulses") >= 0 && shown.indexOf("d_rndc_pulses") >= 0 &&
		shown.indexOf("dl_rndc_pulses") < 0 && shown.indexOf("d_rndSeq") >= 0);
	check("RANDOM set from the engine (pulses " + d.disp.d_rnd_pulses + ", per cycle " + d.disp.d_rndc_pulses + ", rate " + d.disp.d_rndRate + ")",
		d.disp.d_rnd_pulses === 0 && d.disp.d_rndc_pulses === 1 && d.disp.d_rndc_velocity === 0 && d.disp.d_rndRate === 9);
	check("no random amounts: the RANDOM tab unmarked (" + d.shown.d_rndmark + ")", !d.shown.d_rndmark);
	b.disp("rnd_velocity", -35);
	check("a random amount: the RANDOM tab marked", d.shown.d_rndmark === true);
	b.displayPage(4);
	check("the mark stays on other pages", d.shown.d_rndmark === true);
	b.displayPage(8);
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
	b.displayPage(2);
	check("CYCLE shows its rows (" + shownNames(d) + ")",
		shownNames(d) === "d_cycleClear d_cycleCount d_cycleEdit d_cycleMute d_cyclePlaying d_cycleSelect dl_cycleClear dl_cycleCount dl_cycleEdit dl_cycleMute dl_cycleSelect");
	check("one cycle at first (" + d.disp.d_cycleCount + ", " + d.disp.dr_cycleClear.join(" ") + ")", d.disp.d_cycleCount === 0 && d.disp.dr_cycleClear.join() === "ALL,HITS");
	s.w.wait(200);
	b.disp("cycleCount", 1);
	s.w.wait(200);
	b.disp("cycleSelect", 1);
	check("CYCLE tab unframed (" + d.shown.d_cycmark + ")", !d.shown.d_cycmark);
	b.disp("cycleEdit", 1);
	check("EDIT on: the CYCLE tab framed red, the line says so (" + d.disp.d_cyclePlaying + ")",
		d.shown.d_cycmark === true && /^EDIT: CYCLE 2 PLAYS ALONE/.test(d.disp.d_cyclePlaying));
	check("Cycle edit: the dials show cycle 2's values (pulses " + b.dialValue(0, "pulses") + ")", b.dialValue(0, "pulses") === 9);
	turn(s.brain, 0, "pulses", 3);
	check("Cycle edit: a dial overrides cycle 2 only (" + b.stepper.getCycleParam(0, 1, "pulses") + ", track " + b.stepper.getParam(0, "pulses") + ")",
		b.stepper.getCycleParam(0, 1, "pulses") === 3 && b.stepper.getParam(0, "pulses") === 9 && b.stepper.getCycleParam(0, 0, "pulses") === 9);
	b.displayPage(2);
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
	b.displayPage(2);
	b.disp("cycleEdit", 0);
	check("EDIT off: the frame goes", d.shown.d_cycmark === false);
	check("Cycle edit off: the dials show the track's values", b.dialValue(0, "pulses") === 9);
	var t1;
	st.setSoloCycle(0, -1);
	st.forget();
	want = emptyWant();
	b.displayPage(2);
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
	b.displayPage(2);
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
