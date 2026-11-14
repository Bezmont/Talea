// The display's drawings, for [v8ui]: the diagram band under a page's rows and the board
// under TRACK's and KEYS' first row, as the Rack module's display draws them (Live's LCD
// colours from [live.colors] in the patch):
//
//   groove label amount v0 .. v128   GROOVE: the accent shape over its span (dim: the full
//                                    shape, bright: scaled by Accent), values -1..1
//   notes label dots n0 n1 ...       PHRASE: the notes over the phrase as a stepped line;
//                                    STYLE (dots 1): the next notes, a dash each
//   grid label l0 .. l15             PHRASE's User shape: a column per 16th of the phrase,
//                                    its level 0..11; click or drag sets a column
//   steps n played now h0 h1 ...     TRACK: the track's n steps, 16 to a row: h is 1 hit
//                                    + 2 locked (a bar along the bottom) + 4 selected (blue),
//                                    those past `played` cut off by Length, the step
//                                    playing (`now`, -1 none)
//   keys label first octaves scale root k p1..pk m l1..lm
//                                    KEYS: `octaves` octaves of keys from MIDI note `first`;
//                                    in the scale (12 bits from C) or in the pool (the k
//                                    notes p) they can be clicked; the pool lit, the root
//                                    marked, the m notes l starting now dotted
//   scalekeys scale root             SCALE: one octave; a key is lit if in the scale
//   lanes edit now c0..c15 L [name lo hi v0..v15]... more
//                                    STEP: the step page's 16 steps (c: 0 past the Steps,
//                                    else 1 + 2 hit + 4 selected + 8 locked + 16 cut off),
//                                    `now` the playing one (-1), `edit` the row's label;
//                                    then L lanes of locked parameters, each step's lock as
//                                    a bar ("x": none); `more` 1 if more are locked
//   play x                           the playhead, 0..1 across (-1: none)
//   clear                            nothing
//
// Clicks go out of the outlet: "grid col level" (drag too), "key note", "scale pc",
// "lane l step value" (0..1 up the lane; drag too, in the lane it started in), Ctrl+click
// "laneclear l step"; on the STEPS row "stepsel step add" (add 1: Ctrl+click).

inlets = 1;
outlets = 1;

mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

var LEVELS = 12, COLUMNS = 16;
var kind = "clear", label = "", data = [], amount = 1, dots = false, playX = -1;
var fg = [0.95, 0.6, 0.2, 1], dim = [0.55, 0.55, 0.55, 1];

function groove(text, amt) {
	kind = "groove";
	label = String(text);
	amount = Number(amt);
	data = Array.prototype.slice.call(arguments, 2).map(Number);
	mgraphics.redraw();
}

function notes(text, d) {
	kind = "notes";
	label = String(text);
	dots = Number(d) !== 0;
	data = Array.prototype.slice.call(arguments, 2).map(Number);
	mgraphics.redraw();
}

function grid(text) {
	kind = "grid";
	label = String(text);
	data = Array.prototype.slice.call(arguments, 1).map(Number);
	mgraphics.redraw();
}

var keyFirst = 36, keyOctaves = 4, scaleBits = 0, root = 0, pool = [], lit = [], stepsData = null;

function keys(text, first, octaves, bits, r) {
	kind = "keys";
	label = String(text);
	keyFirst = Number(first);
	keyOctaves = Number(octaves);
	scaleBits = Number(bits);
	root = Number(r);
	var a = Array.prototype.slice.call(arguments, 5).map(Number), k = a[0] || 0;
	pool = a.slice(1, 1 + k);
	lit = a.slice(2 + k, 2 + k + (a[1 + k] || 0));
	mgraphics.redraw();
}

function scalekeys(bits, r) {
	kind = "scalekeys";
	label = "";
	keyFirst = 60;
	keyOctaves = 1;
	scaleBits = Number(bits);
	root = Number(r);
	pool = [];
	lit = [];
	mgraphics.redraw();
}

function steps() {
	var a = Array.prototype.slice.call(arguments).map(Number);
	kind = "steps";
	label = "";
	stepsData = { n: a[0], played: a[1], now: a[2], hits: a.slice(3) };
	mgraphics.redraw();
}

var lanesData = null;

function lanes() {
	var a = Array.prototype.slice.call(arguments), i = 0;
	kind = "lanes";
	label = "";
	var edit = String(a[i++]), now = Number(a[i++]), cells = a.slice(i, i + 16).map(Number);
	i += 16;
	var n = Number(a[i++]), list = [];
	for (var l = 0; l < n; l++) {
		var lane = { name: String(a[i++]), lo: Number(a[i++]), hi: Number(a[i++]), v: [] };
		for (var k = 0; k < 16; k++) {
			var v = a[i++];
			lane.v.push(String(v) === "x" ? NaN : Number(v));
		}
		list.push(lane);
	}
	lanesData = { edit: edit, now: now, cells: cells, list: list, more: Number(a[i]) === 1 };
	mgraphics.redraw();
}

function clear() {
	kind = "clear";
	data = [];
	mgraphics.redraw();
}

// The playhead, as a fraction of the width.
// (A message has a function of its own: a variable of the same name could take it.)
function play(x) {
	playX = Number(x);
	mgraphics.redraw();
}

// Colours from [live.colors]: "live_lcd_control_fg r g b a" (the shapes), "live_lcd_title
// r g b a" (labels, axes, the full groove shape).
function anything() {
	var a = Array.prototype.slice.call(arguments);
	if (a.length < 3) return;
	var c = [Number(a[0]), Number(a[1]), Number(a[2]), a.length > 3 ? Number(a[3]) : 1];
	if (messagename === "live_lcd_control_fg") fg = c;
	else if (messagename === "live_lcd_title") dim = c;
	mgraphics.redraw();
}

function size() {
	return mgraphics.size || [box.rect[2] - box.rect[0], box.rect[3] - box.rect[1]];
}

// The plotting area, under the corner label.
var TOP = 12, PAD = 2;

function paint() {
	var s = size(), w = s[0], h = s[1];
	if (kind === "clear") return;
	if (kind === "steps") return paintSteps(w, h);
	if (kind === "lanes") return paintLanes(w, h);
	if (kind === "keys" || kind === "scalekeys") return paintKeys(w, h);
	var x0 = PAD, x1 = w - PAD, top = TOP, bottom = h - PAD;
	// The corner label, top right.
	mgraphics.select_font_face("Arial");
	mgraphics.set_font_size(8.5);
	var m = mgraphics.text_measure(label);
	mgraphics.set_source_rgba(dim);
	mgraphics.move_to(w - PAD - m[0], 9);
	mgraphics.show_text(label);
	if (kind === "groove") paintGroove(x0, x1, top, bottom);
	else if (kind === "notes") paintNotes(x0, x1, top, bottom);
	else if (kind === "grid") paintGrid(x0, x1, top, bottom);
	if (playX >= 0 && kind !== "grid" && !(kind === "notes" && dots)) {
		var x = x0 + (x1 - x0) * playX;
		mgraphics.set_source_rgba(dim);
		mgraphics.set_line_width(1);
		mgraphics.move_to(Math.round(x) + 0.5, top);
		mgraphics.line_to(Math.round(x) + 0.5, bottom);
		mgraphics.stroke();
	}
}

function paintGroove(x0, x1, top, bottom) {
	var mid = (top + bottom) / 2, n = data.length;
	if (n < 2) return;
	mgraphics.set_source_rgba(dim[0], dim[1], dim[2], 0.5);
	mgraphics.set_line_width(1);
	mgraphics.move_to(x0, Math.round(mid) + 0.5);
	mgraphics.line_to(x1, Math.round(mid) + 0.5);
	mgraphics.stroke();
	for (var pass = 0; pass < 2; pass++) {
		var scale = pass === 0 ? 1 : amount;
		for (var i = 0; i < n; i++) {
			var x = x0 + (x1 - x0) * i / (n - 1), y = mid - data[i] * scale * (mid - top);
			if (i === 0) mgraphics.move_to(x, y);
			else mgraphics.line_to(x, y);
		}
		mgraphics.set_source_rgba(pass === 0 ? dim : fg);
		mgraphics.set_line_width(pass === 0 ? 1 : 1.5);
		mgraphics.stroke();
	}
}

// Notes from the lowest to the highest (at least an octave), as Rack draws them.
function paintNotes(x0, x1, top, bottom) {
	var n = data.length;
	if (!n) return;
	var lo = Math.min.apply(null, data), hi = Math.max(Math.max.apply(null, data), lo + 12);
	var cw = (x1 - x0) / n;
	mgraphics.set_source_rgba(fg);
	for (var i = 0; i < n; i++) {
		var x = x0 + i * cw, y = bottom - (bottom - top) * (data[i] - lo) / (hi - lo);
		if (dots) mgraphics.rectangle(x + cw * 0.2, y - 1.5, cw * 0.6, 3);
		else mgraphics.rectangle(x, y - 1, cw + 0.5, 2);
		mgraphics.fill();
	}
}

function gridRect() {
	var s = size();
	return [PAD, TOP, s[0] - 2 * PAD, s[1] - PAD - TOP];
}

// The User shape: the set cell of each column bright, the column under it faint; the
// playing column lighter.
function paintGrid(x0, x1, top, bottom) {
	var r = gridRect(), cw = r[2] / COLUMNS, ch = r[3] / LEVELS;
	var playCol = playX >= 0 ? Math.min(COLUMNS - 1, Math.floor(playX * COLUMNS)) : -1;
	for (var c = 0; c < COLUMNS; c++) {
		var level = data[c] || 0;
		for (var l = 0; l <= level; l++) {
			var on = l === level;
			if (on && c === playCol) mgraphics.set_source_rgba(Math.min(1, fg[0] + 0.2), Math.min(1, fg[1] + 0.2), Math.min(1, fg[2] + 0.2), 1);
			else if (on) mgraphics.set_source_rgba(fg);
			else mgraphics.set_source_rgba(fg[0], fg[1], fg[2], 0.2);
			mgraphics.rectangle(r[0] + c * cw + 0.5, r[1] + (LEVELS - 1 - l) * ch + 0.5, cw - 1, ch - 1);
			mgraphics.fill();
		}
	}
	mgraphics.set_source_rgba(dim);
	mgraphics.set_line_width(1);
	mgraphics.move_to(r[0], r[1] + r[3] + 0.5);
	mgraphics.line_to(r[0] + r[2], r[1] + r[3] + 0.5);
	mgraphics.stroke();
}

// TRACK: the steps, 16 to a row, lit as the pads are.
function paintSteps(w, h) {
	if (!stepsData) return;
	var rows = Math.max(1, Math.ceil(stepsData.n / 16)), cw = (w - 2 * PAD) / 16;
	var ch = Math.min(18, (h - 2 * PAD) / rows - 3);
	for (var i = 0; i < stepsData.n; i++) {
		var x = PAD + (i % 16) * cw, y = PAD + Math.floor(i / 16) * (ch + 3);
		var v = stepsData.hits[i];
		stepCell(x, y, cw, ch, v & 1, i >= stepsData.played, i === stepsData.now, v & 4, v & 2);
	}
}

// A step as the pads show it, but selected steps steady blue and locked ones with a
// light-blue bar along the bottom (the pads blink the selection; they can't draw a bar).
function stepCell(x, y, cw, ch, hit, cut, now, sel, locked) {
	if (now && hit) mgraphics.set_source_rgba(1, 1, 1, 1);
	else if (now) mgraphics.set_source_rgba(dim[0], dim[1], dim[2], 0.7);
	else if (sel) mgraphics.set_source_rgba(hit ? 0.3 : 0.08, hit ? 0.6 : 0.2, hit ? 1 : 0.5, 1);
	else if (hit) mgraphics.set_source_rgba(fg[0], fg[1], fg[2], cut ? 0.3 : 1);
	else mgraphics.set_source_rgba(dim[0], dim[1], dim[2], cut ? 0.06 : 0.16);
	mgraphics.rectangle(x + 1, y, cw - 2, ch);
	mgraphics.fill();
	if (locked) {
		mgraphics.set_source_rgba(0.45, 0.8, 0.95, 1);
		mgraphics.rectangle(x + 1, y + ch - 3, cw - 2, 3);
		mgraphics.fill();
	}
}

// STEP: the edit row (the page's steps, lit as the pads), then a lane per locked parameter.
var LANE_X = 56, LANE_TOP = 18, EDIT_H = 13, LOCK_BLUE = [0.3, 0.6, 1, 1];

function laneCellW(w) {
	return (w - PAD - LANE_X) / 16;
}

function laneGeom(l, n, h) {
	var avail = h - PAD - LANE_TOP, lh = Math.min(16, avail / Math.max(n, 1) - 2);
	return [LANE_TOP + l * (lh + 2), lh];
}

function laneLabel(text, y, lh, c) {
	mgraphics.select_font_face("Arial");
	mgraphics.set_font_size(Math.min(9, lh + 1));
	mgraphics.set_source_rgba(c);
	mgraphics.move_to(PAD, y + lh / 2 + 3);
	mgraphics.show_text(text);
}

function paintLanes(w, h) {
	if (!lanesData) return;
	var cw = laneCellW(w);
	laneLabel(lanesData.edit, PAD, EDIT_H, lanesData.edit === "STEPS" ? dim : [1, 0.25, 0.2, 1]);
	for (var i = 0; i < 16; i++) {
		var c = lanesData.cells[i], x = LANE_X + i * cw;
		if (!c) continue;
		stepCell(x, PAD, cw, EDIT_H, c & 2, c & 16, i === lanesData.now, c & 4, c & 8);
	}
	var n = lanesData.list.length;
	if (!n) {
		mgraphics.select_font_face("Arial");
		mgraphics.set_font_size(9);
		mgraphics.set_source_rgba(dim);
		mgraphics.move_to(LANE_X, LANE_TOP + 20);
		mgraphics.show_text("NO LOCKS: CTRL+CLICK STEP PADS, THEN TURN A DIAL");
		mgraphics.move_to(LANE_X, LANE_TOP + 34);
		mgraphics.show_text("LANES HERE: CLICK / DRAG = SET A STEP'S LOCK, CTRL+CLICK = REMOVE");
		return;
	}
	for (var l = 0; l < n; l++) {
		var lane = lanesData.list[l], g = laneGeom(l, n, h), y = g[0], lh = g[1];
		laneLabel(lane.name + (l === n - 1 && lanesData.more ? "+" : ""), y, lh, LOCK_BLUE);
		var bipolar = lane.lo < 0;
		for (i = 0; i < 16; i++) {
			if (!lanesData.cells[i]) continue;
			x = LANE_X + i * cw;
			mgraphics.set_source_rgba(dim[0], dim[1], dim[2], 0.1);
			mgraphics.rectangle(x + 1, y, cw - 2, lh);
			mgraphics.fill();
			var v = lane.v[i];
			if (!isNaN(v)) {
				var norm = Math.max(0, Math.min(1, (v - lane.lo) / (lane.hi - lane.lo))), from = bipolar ? 0.5 : 0;
				var lo = Math.min(from, norm), hi = Math.max(from, norm);
				mgraphics.set_source_rgba(LOCK_BLUE);
				mgraphics.rectangle(x + 1, y + lh * (1 - hi), cw - 2, Math.max(lh * (hi - lo), 1));
				mgraphics.fill();
			}
			if (i === lanesData.now) {
				mgraphics.set_source_rgba(dim);
				mgraphics.set_line_width(1);
				mgraphics.rectangle(x + 1.5, y + 0.5, cw - 3, lh - 1);
				mgraphics.stroke();
			}
		}
	}
}

// The lane and step under (x, y), and the value there (0..1 up the lane); `only`: keep to
// that lane (a drag).
function laneAt(x, y, only) {
	if (!lanesData || !lanesData.list.length) return null;
	var s = size(), cw = laneCellW(s[0]), n = lanesData.list.length;
	var step = Math.floor((x - LANE_X) / cw);
	if (only !== undefined) step = Math.max(0, Math.min(15, step));
	if (step < 0 || step > 15 || !lanesData.cells[step]) return null;
	for (var l = 0; l < n; l++) {
		if (only !== undefined && l !== only) continue;
		var g = laneGeom(l, n, s[1]);
		if (only === undefined && (y < g[0] || y > g[0] + g[1])) continue;
		var norm = Math.max(0, Math.min(1, 1 - (y - g[0]) / g[1]));
		return { lane: l, step: step, value: Math.round(norm * 1000) / 1000 };
	}
	return null;
}

var laneDrag = -1, laneSent = "";
function laneEdit(x, y, remove) {
	var at = laneAt(x, y, laneDrag >= 0 ? laneDrag : undefined);
	if (!at) return;
	if (remove) return outlet(0, "laneclear", at.lane, at.step);
	if (laneDrag < 0) laneDrag = at.lane;
	var key = at.lane + " " + at.step + " " + at.value;
	if (key === laneSent) return;
	laneSent = key;
	var lane = lanesData.list[at.lane];
	lane.v[at.step] = lane.lo + at.value * (lane.hi - lane.lo); // drawn at once
	mgraphics.redraw();
	outlet(0, "lane", at.lane, at.step, at.value);
}

var BLACK = [false, true, false, true, false, false, true, false, true, false, true, false];

// Keys are as wide as KEYS' (4 octaves across the box); SCALE's one octave starts under the
// rows' controls, NOTES to its left.
var KEY_OCTAVES = 4, SCALE_KEYS_X = 56;

// The keys' rectangles, white keys first so the black ones draw over them: [note, x, y, w, h].
function keyRects(w, h) {
	var ww = (w - 2 * PAD) / (7 * KEY_OCTAVES), out = [], blacks = [], wi = 0;
	var x0 = kind === "keys" ? PAD : SCALE_KEYS_X, top = kind === "keys" ? TOP : PAD, kh = h - top - PAD;
	for (var n = keyFirst; n < keyFirst + 12 * keyOctaves; n++) {
		if (BLACK[n % 12]) blacks.push([n, x0 + wi * ww - ww * 0.3, top, ww * 0.6, kh * 0.6]);
		else out.push([n, x0 + (wi++) * ww, top, ww, kh]);
	}
	return out.concat(blacks);
}

function inScaleKey(n) {
	return (scaleBits >> (n % 12)) & 1;
}

// The keys, as Rack's: a key in the scale or the pool can be clicked (the others greyed),
// the pool lit, the root marked under it, a note starting now dotted; C names on KEYS.
function paintKeys(w, h) {
	if (kind === "scalekeys") {
		mgraphics.select_font_face("Arial");
		mgraphics.set_font_size(9);
		mgraphics.set_source_rgba(dim);
		mgraphics.move_to(PAD, PAD + 11);
		mgraphics.show_text("NOTES");
	}
	if (kind === "keys" && label) {
		mgraphics.select_font_face("Arial");
		mgraphics.set_font_size(8.5);
		var m = mgraphics.text_measure(label);
		mgraphics.set_source_rgba(dim);
		mgraphics.move_to(w - PAD - m[0], 9);
		mgraphics.show_text(label);
	}
	keyRects(w, h).forEach(function (k) {
		var n = k[0], black = BLACK[n % 12], inPool = pool.indexOf(n) >= 0;
		var on = kind === "scalekeys" ? inScaleKey(n) : inPool, enabled = inPool || inScaleKey(n);
		if (black) {
			mgraphics.set_source_rgba(0.04, 0.04, 0.05, 1);
			mgraphics.rectangle(k[1], k[2], k[3], k[4]);
			mgraphics.fill();
		}
		if (on) mgraphics.set_source_rgba(fg);
		else mgraphics.set_source_rgba(dim[0], dim[1], dim[2], (enabled ? 0.3 : 0.08) * (black ? 0.7 : 1));
		mgraphics.rectangle(k[1] + 0.5, k[2], k[3] - 1, k[4]);
		mgraphics.fill();
		if ((n - root) % 12 === 0 && enabled) {
			mgraphics.set_source_rgba(on ? 0 : fg[0], on ? 0 : fg[1], on ? 0 : fg[2], 0.7);
			mgraphics.rectangle(k[1] + k[3] * 0.25, k[2] + k[4] - 4, k[3] * 0.5, 2);
			mgraphics.fill();
		}
		if (lit.indexOf(n) >= 0) {
			mgraphics.set_source_rgba(1, 1, 1, 1);
			mgraphics.ellipse(k[1] + k[3] / 2 - 2.5, k[2] + k[4] * (black ? 0.65 : 0.76) - 2.5, 5, 5); // white: below the black keys
			mgraphics.fill();
		}
		if (n % 12 === 0) {
			mgraphics.select_font_face("Arial");
			mgraphics.set_font_size(7.5);
			mgraphics.set_source_rgba(on ? 0 : dim[0], on ? 0 : dim[1], on ? 0 : dim[2], 1);
			mgraphics.move_to(k[1] + 2, k[2] + k[4] - 6);
			mgraphics.show_text(kind === "keys" ? "C" + (Math.floor(n / 12) - 1) : "C");
		}
	});
}

// A click on a key: the black keys first (they're on top).
function keyAt(x, y) {
	var s = size(), rects = keyRects(s[0], s[1]);
	for (var i = rects.length - 1; i >= 0; i--) {
		var k = rects[i];
		if (x >= k[1] && x < k[1] + k[3] && y >= k[2] && y < k[2] + k[4]) return k[0];
	}
	return -1;
}

// Click and drag on the grid: the column under the mouse to the level under it.
var lastSent = "";
function edit(x, y) {
	if (kind !== "grid") return;
	var r = gridRect();
	if (x < r[0] || x > r[0] + r[2] || y < r[1] - 4 || y > r[1] + r[3] + 4) return;
	var col = Math.max(0, Math.min(COLUMNS - 1, Math.floor((x - r[0]) / r[2] * COLUMNS)));
	var level = Math.max(0, Math.min(LEVELS - 1, LEVELS - 1 - Math.floor((y - r[1]) / r[3] * LEVELS)));
	var key = col + " " + level;
	if (key === lastSent) return;
	lastSent = key;
	data[col] = level; // drawn at once; the script's next "grid" confirms it
	mgraphics.redraw();
	outlet(0, "grid", col, level);
}

function onclick(x, y, but, cmd, shift, caps, option, ctrl) {
	lastSent = "";
	if (kind === "lanes") {
		laneDrag = -1;
		laneSent = "";
		if (y >= PAD && y <= PAD + EDIT_H && lanesData) {
			// The STEPS row: select the step (Ctrl+click: add or remove it).
			var step = Math.floor((x - LANE_X) / laneCellW(size()[0]));
			if (step >= 0 && step < 16 && lanesData.cells[step]) outlet(0, "stepsel", step, cmd || ctrl ? 1 : 0);
			return;
		}
		return laneEdit(x, y, !!(cmd || ctrl));
	}
	if (kind === "keys" || kind === "scalekeys") {
		var n = keyAt(x, y);
		if (n < 0) return;
		if (kind === "keys") outlet(0, "key", n);
		else outlet(0, "scale", n % 12);
		return;
	}
	edit(x, y);
}

function ondrag(x, y, but) {
	if (kind === "lanes") {
		if (but && laneDrag >= 0) laneEdit(x, y, false);
		else if (!but) laneDrag = -1;
		return;
	}
	if (but) edit(x, y);
}
