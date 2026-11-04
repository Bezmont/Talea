#include "plugin.hpp"
#include "Widgets.hpp"
#include "OutLayout.hpp"
#include "Expander.hpp"

using namespace talea;

/** OUT expander (§3.12): per-track poly Pitch / Gate / Velocity, one channel per chord note.
No params; everything comes from the Talea to its left. The channel count only changes on a
new note, and pitch / velocity hold between gates. */
struct TaleaOut : Module {
	enum ParamId { PARAMS_LEN };
	enum InputId { INPUTS_LEN };
	enum OutputId {
		ENUMS(PITCH_OUTPUT, kNumTracks),
		ENUMS(GATE_OUTPUT, kNumTracks),
		ENUMS(VELOCITY_OUTPUT, kNumTracks),
		OUTPUTS_LEN
	};
	enum LightId {
		ENUMS(TRACK_LIGHT, kNumTracks * 3),
		LINK_LIGHT,
		LIGHTS_LEN
	};

	OutMessage messages[2];
	dsp::ClockDivider lightDivider;

	TaleaOut() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int t = 0; t < kNumTracks; t++) {
			configOutput(PITCH_OUTPUT + t, string::f("Track %d pitch (V/oct, one channel per chord note)", t + 1));
			configOutput(GATE_OUTPUT + t, string::f("Track %d gate", t + 1));
			configOutput(VELOCITY_OUTPUT + t, string::f("Track %d velocity (0-10 V)", t + 1));
		}
		leftExpander.producerMessage = &messages[0];
		leftExpander.consumerMessage = &messages[1];
		lightDivider.setDivision(64);
	}

	bool linked() {
		return leftExpander.module && leftExpander.module->model == modelTalea;
	}

	void process(const ProcessArgs& args) override {
		bool link = linked();
		const OutMessage& m = *static_cast<const OutMessage*>(leftExpander.consumerMessage);
		for (int t = 0; t < kNumTracks; t++) {
			int n = link ? clamp(m.channels[t], 1, kMaxPool) : 1;
			bool gate = link && m.gate[t];
			outputs[PITCH_OUTPUT + t].setChannels(n);
			outputs[GATE_OUTPUT + t].setChannels(n);
			outputs[VELOCITY_OUTPUT + t].setChannels(n);
			for (int c = 0; c < n; c++) {
				outputs[PITCH_OUTPUT + t].setVoltage(link ? m.pitch[t][c] : 0.f, c);
				outputs[GATE_OUTPUT + t].setVoltage(gate ? 10.f : 0.f, c);
				outputs[VELOCITY_OUTPUT + t].setVoltage(link ? m.velocity[t] : 0.f, c);
			}
		}
		if (!lightDivider.process())
			return;
		lights[LINK_LIGHT].setBrightness(link);
		// White on gate, blue-grey when muted, dim orange otherwise.
		for (int t = 0; t < kNumTracks; t++) {
			float r = 0.f, g = 0.f, b = 0.f;
			if (link && m.gate[t])
				r = g = b = 1.f;
			else if (link && m.muted[t]) {
				r = 0.08f;
				g = 0.11f;
				b = 0.18f;
			}
			else if (link) {
				r = 0.24f;
				g = 0.14f;
				b = 0.04f;
			}
			lights[TRACK_LIGHT + 3 * t + 0].setBrightness(r);
			lights[TRACK_LIGHT + 3 * t + 1].setBrightness(g);
			lights[TRACK_LIGHT + 3 * t + 2].setBrightness(b);
		}
	}
};

struct TaleaOutWidget : ModuleWidget {
	TaleaOutWidget(TaleaOut* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/TaleaOut.svg")));
		addChild(new PanelLabels(out_layout::kLabels));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		namespace L = out_layout;
		using M = TaleaOut;
		auto at = [](L::Pos p) { return mm2px(Vec(p.x, p.y)); };
		addChild(createLightCentered<SmallLight<GreenLight>>(at(L::LINK_LIGHT), module, M::LINK_LIGHT));
		for (int t = 0; t < kNumTracks; t++) {
			addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(at(L::TRACK_LIGHT[t]), module, M::TRACK_LIGHT + 3 * t));
			addOutput(createOutputCentered<PJ301MPort>(at(L::PITCH_OUTPUT[t]), module, M::PITCH_OUTPUT + t));
			addOutput(createOutputCentered<PJ301MPort>(at(L::GATE_OUTPUT[t]), module, M::GATE_OUTPUT + t));
			addOutput(createOutputCentered<PJ301MPort>(at(L::VELOCITY_OUTPUT[t]), module, M::VELOCITY_OUTPUT + t));
		}
	}
};

Model* modelTaleaOut = createModel<TaleaOut, TaleaOutWidget>("TaleaOut");
