// Loads Talea's engine (talea_engine.js, from build.py) in Max's v8 and plays a bar into
// the Max Console. Put [v8 max_check.js] in a patch whose folder holds both files, connect
// a button to it, and click it.
autowatch = 1;

function say(s) { post("max_check: " + s + "\n"); }

// Max's v8 may or may not have require(); if not, read the file and run it ourselves.
function load() {
	var how = [], M = null;
	if (typeof require === "function") {
		var names = ["talea_engine", "talea_engine.js", "./talea_engine.js"];
		for (var i = 0; i < names.length && !M; i++) {
			try { M = require(names[i]); how.push("require('" + names[i] + "')"); }
			catch (e) { how.push("require('" + names[i] + "') failed: " + e); }
		}
	} else how.push("no require()");
	if (!M || typeof M.cwrap !== "function") {
		var f = new File("talea_engine.js", "read", "TEXT");
		if (!f.isopen) throw new Error("can't find talea_engine.js next to this script");
		var text = "";
		while (f.position < f.eof) {
			var chunk = f.readstring(16384);
			if (!chunk || !chunk.length) break;
			text += chunk;
		}
		f.close();
		how.push("read " + text.length + " characters with File");
		try { M = (0, eval)(text + "\n;Module"); }
		catch (e) { throw new Error(e + " (after " + how.join("; ") + ")"); }
	}
	return { M: M, how: how.join("; ") };
}

function bang() {
	var t0 = Date.now(), r;
	try { r = load(); } catch (e) { say("loading failed: " + e); return; }
	var M = r.M;
	say("loaded in " + (Date.now() - t0) + " ms via " + r.how);
	if (!M || typeof M.cwrap !== "function" || typeof M._talea_create !== "function") {
		say("the module isn't ready right after loading");
		return;
	}
	var w = function (n, ret, args) { return M.cwrap("talea_" + n, ret, args); };
	var create = w("create", "number", []), setParam = w("set_param", null, ["number", "number", "number", "number"]);
	var proc = w("process", "number", ["number", "number", "number"]);
	var time = w("event_time", "number", ["number", "number"]), track = w("event_track", "number", ["number", "number"]);
	var on = w("event_on", "number", ["number", "number"]), note = w("event_note", "number", ["number", "number"]);
	var h = create();
	say("engine created: " + (h !== 0));
	// Track 1: 4 pulses. Track 3: every 16th, swung (every 2nd note late).
	setParam(h, 0, 1, 4);
	setParam(h, 2, 1, 16);
	setParam(h, 2, 13, 60);

	var EPS = 1e-9, lines = [], worst = 0, t1 = Date.now();
	for (var s = 0; s < 16; s++) {
		var a = Date.now();
		var n = proc(h, s * 0.25 - EPS, (s + 1) * 0.25 - EPS);
		worst = Math.max(worst, Date.now() - a);
		for (var i = 0; i < n; i++)
			if (on(h, i)) lines.push("t" + (track(h, i) + 1) + "@" + time(h, i).toFixed(3));
	}
	say("one bar: " + lines.length + " notes in " + (Date.now() - t1) + " ms (slowest 16th: " + worst + " ms)");
	say(lines.slice(0, 12).join(" "));
	var ok = lines.indexOf("t1@0.000") >= 0 && lines.indexOf("t3@0.325") >= 0 && lines.length === 20;
	say(ok ? "PASS: the notes are as expected" : "FAIL: expected 20 notes including t1@0.000 and t3@0.325");
}
