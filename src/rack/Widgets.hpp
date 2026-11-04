#pragma once
#include "plugin.hpp"
#include "PanelLayout.hpp"

namespace talea {

inline Vec mm(layout::Pos p) {
	return mm2px(Vec(p.x, p.y));
}

// ---------------------------------------------------------------- lights

template <typename TBase = GrayModuleLightWidget>
struct TOrangeLight : TBase {
	TOrangeLight() {
		this->addBaseColor(nvgRGB(0xf0, 0x8a, 0x24));
	}
};
using OrangeLight = TOrangeLight<>;

/** Copy / Paste / Clear: green = track or parameter clipboard, blue = pattern clipboard. */
template <typename TBase = GrayModuleLightWidget>
struct TGreenBlueLight : TBase {
	TGreenBlueLight() {
		this->addBaseColor(SCHEME_GREEN);
		this->addBaseColor(SCHEME_BLUE);
	}
};
using GreenBlueLight = TGreenBlueLight<>;

// ---------------------------------------------------------------- buttons and knobs

struct SmallButton : TL1105 {
	SmallButton() {
		momentary = true;
	}
};

/** Small lit button for step pages and Follow. */
using SmallLightButton = LightButton<SmallButton, SmallSimpleLight<OrangeLight>>;

constexpr float kEndlessRange = 10000.f;

/** Knob with no end stops (Data, Harmony). The param counts detents; the module reads deltas.
Handles dragging itself: Rack's Knob scales drag speed by the param range, which makes a
huge-range knob unusable. */
template <typename TBase>
struct TEndlessKnob : TBase {
	static constexpr float kPxPerDetent = 8.f;
	float accum = 0.f;

	TEndlessKnob() {
		this->minAngle = -kEndlessRange * float(M_PI) / 12.f; // 15 degrees per detent
		this->maxAngle = kEndlessRange * float(M_PI) / 12.f;
	}

	void turn(int detents) {
		if (ParamQuantity* pq = this->getParamQuantity())
			pq->setValue(pq->getValue() + detents);
	}

	void onDragStart(const event::DragStart& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		accum = 0.f;
		APP->window->cursorLock();
	}

	void onDragMove(const event::DragMove& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		accum += e.mouseDelta.x - e.mouseDelta.y;
		int detents = int(accum / kPxPerDetent);
		if (detents != 0) {
			turn(detents);
			accum -= detents * kPxPerDetent;
		}
	}

	void onDragEnd(const event::DragEnd& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		APP->window->cursorUnlock();
	}

	void onHoverScroll(const event::HoverScroll& e) override {
		// Respect Rack's View > "Knob scroll wheel" setting like every other knob.
		if (!settings::knobScroll) {
			TBase::onHoverScroll(e);
			return;
		}
		if (e.scrollDelta.y != 0.f) {
			turn(e.scrollDelta.y > 0.f ? 1 : -1);
			e.consume(this);
		}
	}
};

using EndlessKnob = TEndlessKnob<RoundSmallBlackKnob>;
using LargeEndlessKnob = TEndlessKnob<RoundBlackKnob>;

/** Square performance pad with an RGB light drawn inside it. */
struct PadButton : app::Switch {
	int firstLightId = -1;
	/** Set on each left click to whether Ctrl (Cmd on Mac) was held; the module reads it
	when it sees the press. */
	std::atomic<bool>* ctrlFlag = nullptr;
	/** Likewise for Shift. */
	std::atomic<bool>* shiftFlag = nullptr;

	PadButton() {
		momentary = true;
		box.size = mm2px(Vec(layout::kPadSize, layout::kPadSize));
	}

	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (ctrlFlag)
				*ctrlFlag = (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL;
			if (shiftFlag)
				*shiftFlag = (e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT;
		}
		app::Switch::onButton(e);
	}

	void shape(NVGcontext* vg, float inset) {
		nvgBeginPath(vg);
		nvgRoundedRect(vg, inset, inset, box.size.x - 2 * inset, box.size.y - 2 * inset, std::min(mm2px(0.9f), box.size.y / 4));
	}

	void draw(const DrawArgs& args) override {
		shape(args.vg, 0.5f);
		nvgFillColor(args.vg, nvgRGB(0x18, 0x1a, 0x1e));
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, nvgRGB(0x5a, 0x61, 0x6b));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);
		app::Switch::draw(args);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1 && module && firstLightId >= 0) {
			float r = module->lights[firstLightId + 0].getBrightness();
			float g = module->lights[firstLightId + 1].getBrightness();
			float b = module->lights[firstLightId + 2].getBrightness();
			float a = std::max(r, std::max(g, b));
			if (a > 0.01f) {
				shape(args.vg, mm2px(0.8f));
				nvgFillColor(args.vg, nvgRGBAf(r / a, g / a, b / a, a));
				nvgFill(args.vg);
			}
		}
		app::Switch::drawLayer(args, layer);
	}
};

/** Small track-select button above the pads, numbered, with the same RGB light. The
selected track keeps a red outline so it stays visible while the light flashes on notes. */
struct TrackButton : PadButton {
	int index = 0;
	const int* selectedTrack = nullptr;

	TrackButton() {
		box.size = mm2px(Vec(layout::kTrackButtonW, layout::kTrackButtonH));
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		PadButton::drawLayer(args, layer);
		if (layer != 1)
			return;
		if (selectedTrack && *selectedTrack == index) {
			shape(args.vg, 0.75f);
			nvgStrokeColor(args.vg, nvgRGB(0xff, 0x3b, 0x30));
			nvgStrokeWidth(args.vg, 1.5f);
			nvgStroke(args.vg);
		}
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font)
			return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, mm2px(1.9f));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, nvgRGBA(0xd8, 0xdc, 0xe2, 0xc0));
		nvgText(args.vg, box.size.x / 2, box.size.y / 2 + 0.5f, std::to_string(index + 1).c_str(), nullptr);
	}
};

// ---------------------------------------------------------------- panel labels

/** Draws the panel text from PanelLayout.hpp: Rack's SVG renderer skips <text> elements. */
struct PanelLabels : widget::TransparentWidget {
	const layout::Label* labels = layout::kLabels;
	int count = int(sizeof(layout::kLabels) / sizeof(layout::kLabels[0]));

	PanelLabels() = default;
	template <size_t N>
	explicit PanelLabels(const layout::Label (&l)[N]) : labels(l), count(int(N)) {}

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> regular = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		std::shared_ptr<window::Font> bold = APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf"));
		if (!regular || !bold)
			return;
		static const int kAlign[] = {NVG_ALIGN_LEFT, NVG_ALIGN_CENTER, NVG_ALIGN_RIGHT};
		for (int i = 0; i < count; i++) {
			const layout::Label& l = labels[i];
			nvgFontFaceId(args.vg, (l.bold ? bold : regular)->handle);
			nvgFontSize(args.vg, mm2px(l.size) * (l.bold ? 1.05f : 1.f));
			nvgTextAlign(args.vg, kAlign[l.align] | NVG_ALIGN_BASELINE);
			nvgFillColor(args.vg, nvgRGB(l.rgb[0], l.rgb[1], l.rgb[2]));
			Vec p = mm2px(Vec(l.x, l.y));
			nvgText(args.vg, p.x, p.y, l.text, nullptr);
		}
	}
};

// ---------------------------------------------------------------- keyboard

/** How one key of a Keyboard looks. `fill` (alpha 0 = none) colours the key, e.g. selected
notes; a disabled key is drawn greyed out (callers ignore its clicks). */
struct KeyLook {
	bool enabled = true;
	NVGcolor fill = nvgRGBA(0, 0, 0, 0);
	bool marker = false; // a bar along the key's bottom edge (e.g. the root)
	bool dot = false;    // a bright dot (e.g. the note sounding now)
};

/** A piano keyboard drawn into a rectangle: `octaves` octaves from MIDI note `first` (a C).
Drawing and hit testing only, so any display can embed it. Drawn for a dark display: every
key is an outline on the background (black keys filled with the background, so the white
keys' lines don't show through), one border colour throughout. */
struct Keyboard {
	math::Rect rect;
	int first = 60;
	int octaves = 1;
	bool labels = true; // note names on the C keys (C4 = MIDI 60)
	NVGcolor background = nvgRGB(0x0d, 0x0f, 0x12);

	static constexpr int kWhiteOf[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6}; // white key at or below
	static bool black(int pc) {
		return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
	}
	float whiteWidth() const {
		return rect.size.x / (7 * octaves);
	}
	float blackHeight() const {
		return rect.size.y * 0.6f;
	}
	math::Rect keyRect(int note) const {
		int k = note - first, pc = ((k % 12) + 12) % 12;
		float w = whiteWidth();
		float x = rect.pos.x + ((k - pc) / 12 * 7 + kWhiteOf[pc]) * w;
		if (black(pc))
			return math::Rect(x + w * 0.68f, rect.pos.y, w * 0.64f, blackHeight());
		return math::Rect(x, rect.pos.y, w, rect.size.y);
	}
	/** Key under `p`, or -1. Black keys sit on top. */
	int keyAt(Vec p) const {
		if (!rect.contains(p))
			return -1;
		for (int n = first; n < first + 12 * octaves; n++) {
			if (black(n % 12) && keyRect(n).contains(p))
				return n;
		}
		for (int n = first; n < first + 12 * octaves; n++) {
			if (!black(n % 12) && keyRect(n).contains(p))
				return n;
		}
		return -1;
	}

	static NVGcolor edge() {
		return nvgRGB(0x6a, 0x71, 0x7b);
	}

	void draw(NVGcontext* vg, const std::function<KeyLook(int)>& look) const {
		auto key = [&](int n) {
			KeyLook l = look(n);
			bool b = black(n % 12);
			math::Rect r = keyRect(n);
			if (b) {
				nvgBeginPath(vg);
				nvgRect(vg, r.pos.x, r.pos.y, r.size.x, r.size.y);
				nvgFillColor(vg, background);
				nvgFill(vg);
			}
			if (!l.enabled) {
				nvgBeginPath(vg);
				nvgRect(vg, r.pos.x, r.pos.y, r.size.x, r.size.y);
				nvgFillColor(vg, nvgRGBA(0x5a, 0x61, 0x6b, 0x30));
				nvgFill(vg);
			}
			if (b) {
				nvgBeginPath(vg);
				nvgRect(vg, r.pos.x, r.pos.y, r.size.x, r.size.y);
				nvgStrokeColor(vg, edge());
				nvgStrokeWidth(vg, 1.f);
				nvgStroke(vg);
			}
			else {
				// Each edge once, so neighbouring keys share a single line: the key's top and
				// bottom, its left edge, and the last key's right edge.
				float x0 = r.pos.x, x1 = r.pos.x + r.size.x, y0 = r.pos.y, y1 = r.pos.y + r.size.y;
				nvgBeginPath(vg);
				nvgMoveTo(vg, x0, y1);
				nvgLineTo(vg, x0, y0);
				nvgLineTo(vg, x1, y0);
				nvgMoveTo(vg, x0, y1);
				nvgLineTo(vg, x1, y1);
				if (n == first + 12 * octaves - 1)
					nvgLineTo(vg, x1, y0);
				nvgStrokeColor(vg, edge());
				nvgStrokeWidth(vg, 1.f);
				nvgStroke(vg);
			}
			if (l.fill.a > 0.f) {
				nvgBeginPath(vg);
				float m = mm2px(0.5f);
				nvgRoundedRect(vg, r.pos.x + m, r.pos.y + m, r.size.x - 2 * m, r.size.y - 2 * m, mm2px(0.3f));
				nvgFillColor(vg, l.fill);
				nvgFill(vg);
			}
			if (l.marker) {
				nvgBeginPath(vg);
				nvgRect(vg, r.pos.x + r.size.x * 0.2f, r.pos.y + r.size.y - mm2px(1.1f), r.size.x * 0.6f, mm2px(0.5f));
				nvgFillColor(vg, nvgRGB(0x9a, 0xa2, 0xad));
				nvgFill(vg);
			}
			if (l.dot) {
				nvgBeginPath(vg);
				nvgCircle(vg, r.pos.x + r.size.x / 2, r.pos.y + r.size.y - mm2px(b ? 1.6f : 2.6f), mm2px(0.8f));
				nvgFillColor(vg, nvgRGB(0xff, 0xff, 0xff));
				nvgFill(vg);
				nvgStrokeColor(vg, nvgRGB(0x0d, 0x0f, 0x12));
				nvgStrokeWidth(vg, 1.f);
				nvgStroke(vg);
			}
			if (labels && n % 12 == 0) {
				nvgFontSize(vg, mm2px(2.f));
				nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
				nvgFillColor(vg, nvgRGB(0xc8, 0xcd, 0xd4));
				nvgText(vg, r.pos.x + r.size.x / 2, r.pos.y + r.size.y - mm2px(l.dot ? 4.f : 1.6f),
				        string::f("C%d", n / 12 - 1).c_str(), nullptr);
			}
		};
		for (int n = first; n < first + 12 * octaves; n++) {
			if (!black(n % 12))
				key(n);
		}
		for (int n = first; n < first + 12 * octaves; n++) {
			if (black(n % 12))
				key(n);
		}
	}
};

} // namespace talea
