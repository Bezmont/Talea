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
//   steps n played now h0 h1 ...     TRACK: the track's n steps, 16 to a row: hits (h = 1),
//                                    those past `played` cut off by Length, the step
//                                    playing (`now`, -1 none)
//   keys label first octaves scale root k p1..pk m l1..lm
//                                    KEYS: `octaves` octaves of keys from MIDI note `first`;
//                                    in the scale (12 bits from C) or in the pool (the k
//                                    notes p) they can be clicked; the pool lit, the root
//                                    marked, the m notes l starting now dotted
//   scalekeys scale root             SCALE: one octave; a key is lit if in the scale
//   play x                           the playhead, 0..1 across (-1: none)
//   clear                            nothing
//
// Clicks go out of the outlet: "grid col level" (drag too), "key note", "scale pc".

inlets = 1;
outlets = 1;

mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

var LEVELS = 12, COLUMNS = 16;
var kind = "clear", label = "", data = [], amount = 1, dots = false, play = -1;
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

var keyFirst = 36, keyOctaves = 4, scaleBits = 0, root = 0, pool = [], lit = [], steps = null;

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

function steps_() {
	var a = Array.prototype.slice.call(arguments).map(Number);
	kind = "steps";
	label = "";
	steps = { n: a[0], played: a[1], now: a[2], hits: a.slice(3) };
	mgraphics.redraw();
}

function clear() {
	kind = "clear";
	data = [];
	mgraphics.redraw();
}

// The playhead, as a fraction of the width.
function play_(x) {
	play = Number(x);
	mgraphics.redraw();
}

// Colours from [live.colors]: "live_lcd_control_fg r g b a" (the shapes), "live_lcd_title
// r g b a" (labels, axes, the full groove shape). "play" comes here too (a reserved word
// as a function name in some engines).
function anything() {
	var a = Array.prototype.slice.call(arguments);
	if (messagename === "play") return play_(a[0]);
	if (messagename === "steps") return steps_.apply(null, a);
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
	if (play >= 0 && kind !== "grid" && !(kind === "notes" && dots)) {
		var x = x0 + (x1 - x0) * play;
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
	var playCol = play >= 0 ? Math.min(COLUMNS - 1, Math.floor(play * COLUMNS)) : -1;
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
	if (!steps) return;
	var rows = Math.max(1, Math.ceil(steps.n / 16)), cw = (w - 2 * PAD) / 16;
	var ch = Math.min(18, (h - 2 * PAD) / rows - 3);
	for (var i = 0; i < steps.n; i++) {
		var x = PAD + (i % 16) * cw, y = PAD + Math.floor(i / 16) * (ch + 3);
		var hit = steps.hits[i] === 1, cut = i >= steps.played, now = i === steps.now;
		if (now && hit) mgraphics.set_source_rgba(1, 1, 1, 1);
		else if (now) mgraphics.set_source_rgba(dim[0], dim[1], dim[2], 0.7);
		else if (hit) mgraphics.set_source_rgba(fg[0], fg[1], fg[2], cut ? 0.3 : 1);
		else mgraphics.set_source_rgba(dim[0], dim[1], dim[2], cut ? 0.06 : 0.16);
		mgraphics.rectangle(x + 1, y, cw - 2, ch);
		mgraphics.fill();
	}
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

function onclick(x, y) {
	lastSent = "";
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
	if (but) edit(x, y);
}
