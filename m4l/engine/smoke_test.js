// Runs build/talea_engine.js the way Max's v8 would: in a bare context (no require, no
// process, no fetch), calling the engine in the same turn the script ran, before any
// promise could resolve. Then plays a short pattern and prints its events.
//
//   node m4l/engine/smoke_test.js        (Emscripten's node: D:/dev/emsdk/node/...)

const fs = require("fs");
const path = require("path");
const vm = require("vm");

const src = fs.readFileSync(path.join(__dirname, "build", "talea_engine.js"), "utf8");
const lines = [];
const ctx = vm.createContext({ post: (s) => lines.push(String(s)) }) // only what Max gives;
let t0 = process.hrtime.bigint();
vm.runInContext(src + "\n;this.Module = Module;", ctx);
const loadMs = Number(process.hrtime.bigint() - t0) / 1e6;

// Synchronously, in the same turn: is the runtime ready?
const M = ctx.Module;
let ok = true;
function check(name, cond) { console.log((cond ? "PASS " : "FAIL ") + name); ok = ok && cond; }

check("module ready without waiting (" + loadMs.toFixed(1) + " ms to load)", typeof M._talea_create === "function" && typeof M.cwrap === "function");
const fn = (name, ret, args) => M.cwrap("talea_" + name, ret, args);
const create = fn("create", "number", []);
const setParam = fn("set_param", null, ["number", "number", "number", "number"]);
const processW = fn("process", "number", ["number", "number", "number"]);
const ev = {};
for (const k of ["time", "track", "on", "note", "velocity"]) ev[k] = fn("event_" + k, "number", ["number", "number"]);
const paramKey = fn("param_key", "string", ["number"]);
const numParams = fn("num_params", "number", [])();

const keys = [];
for (let p = 0; p < numParams; p++) keys.push(paramKey(p));
check("parameter table (" + numParams + "): " + keys.slice(0, 5).join(", ") + ", ...", keys[0] === "steps" && keys[1] === "pulses");

const h = create();
check("engine created", h !== 0);
const P = (k) => keys.indexOf(k);
setParam(h, 0, P("pulses"), 4);   // track 1: 4 of 16 sixteenths
setParam(h, 1, P("pulses"), 3);   // track 2: 3 of 8, with 2 repeats
setParam(h, 1, P("steps"), 8);
setParam(h, 1, P("repeats"), 2);
setParam(h, 2, P("pulses"), 16);  // track 3: every 16th, swung
setParam(h, 2, P("timing"), 60);

// One bar, a 16th at a time, as the brain would ask for it. Windows are (from, to], so
// each starts just before its step (as Rack's rewind() does) to include a note on it.
const EPS = 1e-9;
const got = [];
t0 = process.hrtime.bigint();
for (let s = 0; s < 16; s++) {
	const n = processW(h, s * 0.25 - EPS, (s + 1) * 0.25 - EPS);
	for (let i = 0; i < n; i++)
		got.push([ev.time(h, i).toFixed(4), ev.track(h, i) + 1, ev.on(h, i) ? "on " : "off", ev.note(h, i), ev.velocity(h, i)].join(" "));
}
const procMs = Number(process.hrtime.bigint() - t0) / 1e6;
console.log("one bar in 16 windows: " + got.length + " events, " + procMs.toFixed(2) + " ms");
console.log("  time   track on/off note vel");
got.forEach((g) => console.log("  " + g));
const ons = (track) => got.filter((g) => g.split(" ")[1] == track && g.includes(" on ")).length;
check("track 1 played 4 notes, the first at time 0", ons(1) === 4 && got.some((g) => g.startsWith("0.0000 1 on")));
check("track 2 played pulses and repeats (" + ons(2) + " notes)", ons(2) > 3);
check("some events off the 16th grid", got.some((g) => Math.abs(parseFloat(g) * 4 - Math.round(parseFloat(g) * 4)) > 1e-6));
if (lines.length) console.log("printed by the module: " + lines.join(" | "));
process.exit(ok ? 0 : 1);
