// A panel section's name, turned a quarter turn counterclockwise, for [v8ui] down the
// side of the section's frame (Max's text boxes can't rotate).
//
//   title SHAPE          the text
//   color r g b a        its colour: Live's theme colour, from [live.colors] in the patch

inlets = 1;
outlets = 0;

mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

var text = "";
var rgba = [0.6, 0.6, 0.6, 1.0];

function title() {
	text = Array.prototype.slice.call(arguments).join(" ");
	mgraphics.redraw();
}

function color(r, g, b, a) {
	rgba = [r, g, b, a === undefined ? 1 : a];
	mgraphics.redraw();
}

// Anything else, e.g. "live_control_fg r g b a" straight from [live.colors].
function anything() {
	var a = Array.prototype.slice.call(arguments);
	if (a.length >= 3) color.apply(null, a.slice(0, 4));
}

function paint() {
	var size = mgraphics.size || [box.rect[2] - box.rect[0], box.rect[3] - box.rect[1]];
	var w = size[0], h = size[1];
	mgraphics.select_font_face("Arial Bold");
	mgraphics.set_font_size(9);
	var m = mgraphics.text_measure(text); // [width, height]
	mgraphics.set_source_rgba(rgba);
	// The text's baseline runs up the box, centred both ways.
	mgraphics.translate(w / 2 + m[1] / 2 - 2, h / 2 + m[0] / 2);
	mgraphics.rotate(-Math.PI / 2);
	mgraphics.move_to(0, 0);
	mgraphics.show_text(text);
}
