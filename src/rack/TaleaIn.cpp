#include "plugin.hpp"
#include "Widgets.hpp"
#include "InLayout.hpp"
#include "Expander.hpp"

using namespace talea;

/** IN expander (§3.12b), placed directly left of Talea: a poly CV input + attenuverter per
modulatable knob parameter, and the performance inputs. It only reads its jacks; Talea does
the rest. */
struct TaleaIn : Module {
	enum ParamId {
		ENUMS(ATTEN_PARAM, kNumModParams),
		PARAMS_LEN
	};
	enum InputId {
		ENUMS(CV_INPUT, kNumModParams),
		TRANSPOSE_INPUT,
		PHRASE_INPUT,
		PATTERN_INPUT,
		TEMP_INPUT,
		MUTE_INPUT,
		MUTE_APPLY_INPUT,
		REGEN_INPUT,
		INPUTS_LEN
	};
	enum OutputId { OUTPUTS_LEN };
	enum LightId { LINK_LIGHT, LIGHTS_LEN };

	dsp::ClockDivider lightDivider;

	TaleaIn() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int i = 0; i < kNumModParams; i++) {
			const char* name = kTrackParams[kModParams[i]].name;
			configParam(ATTEN_PARAM + i, -1.f, 1.f, 1.f, string::f("%s CV amount", name), "%", 0.f, 100.f);
			configInput(CV_INPUT + i, string::f("%s CV (+-5 V = full range; channel n = track n)", name));
		}
		configInput(TRANSPOSE_INPUT, "Transpose (V/oct, channel n = track n)");
		configInput(PHRASE_INPUT, "Phrase CV (channel n = track n)");
		configInput(PATTERN_INPUT, "Pattern select (0-10 V)");
		configInput(TEMP_INPUT, "Temp gate (Temp while high)");
		configInput(MUTE_INPUT, "Mute gates (high = muted; channel n = track n)");
		configInput(MUTE_APPLY_INPUT, "Mute apply (trigger applies staged mutes)");
		configInput(REGEN_INPUT, "Random regenerate (trigger; channel n = track n)");
		lightDivider.setDivision(256);
	}

	/** Channel n to track n; a mono cable feeds every track; unpatched = 0 V. */
	void spread(Input& in, float* out, float gain = 1.f, bool* on = nullptr) {
		int n = in.getChannels();
		for (int t = 0; t < kNumTracks; t++) {
			bool has = n == 1 || t < n;
			out[t] = has ? in.getVoltage(n == 1 ? 0 : t) * gain : 0.f;
			if (on)
				on[t] = has;
		}
	}

	void process(const ProcessArgs& args) override {
		Module* main = rightExpander.module;
		bool link = main && main->model == modelTalea;
		if (lightDivider.process())
			lights[LINK_LIGHT].setBrightness(link);
		if (!link)
			return;
		InMessage* m = static_cast<InMessage*>(main->leftExpander.producerMessage);
		if (!m)
			return;
		for (int i = 0; i < kNumModParams; i++)
			spread(inputs[CV_INPUT + i], m->cv[i], params[ATTEN_PARAM + i].getValue());
		spread(inputs[TRANSPOSE_INPUT], m->transpose);
		spread(inputs[PHRASE_INPUT], m->phrase);
		m->patternOn = inputs[PATTERN_INPUT].isConnected();
		m->pattern = inputs[PATTERN_INPUT].getVoltage();
		m->temp = inputs[TEMP_INPUT].getVoltage();
		spread(inputs[MUTE_INPUT], m->mute, 1.f, m->muteOn);
		m->muteApply = inputs[MUTE_APPLY_INPUT].getVoltage();
		spread(inputs[REGEN_INPUT], m->regen);
		main->leftExpander.requestMessageFlip();
	}
};

struct TaleaInWidget : ModuleWidget {
	TaleaInWidget(TaleaIn* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/TaleaIn.svg")));
		addChild(new PanelLabels(in_layout::kLabels));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		namespace L = in_layout;
		using M = TaleaIn;
		auto at = [](L::Pos p) { return mm2px(Vec(p.x, p.y)); };
		addChild(createLightCentered<SmallLight<GreenLight>>(at(L::LINK_LIGHT), module, M::LINK_LIGHT));
		for (int i = 0; i < kNumModParams; i++) {
			addParam(createParamCentered<Trimpot>(at(L::ATTEN_PARAM[i]), module, M::ATTEN_PARAM + i));
			addInput(createInputCentered<PJ301MPort>(at(L::CV_INPUT[i]), module, M::CV_INPUT + i));
		}
		addInput(createInputCentered<PJ301MPort>(at(L::TRANSPOSE_INPUT), module, M::TRANSPOSE_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::PHRASE_INPUT), module, M::PHRASE_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::PATTERN_INPUT), module, M::PATTERN_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::TEMP_INPUT), module, M::TEMP_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::MUTE_INPUT), module, M::MUTE_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::MUTE_APPLY_INPUT), module, M::MUTE_APPLY_INPUT));
		addInput(createInputCentered<PJ301MPort>(at(L::REGEN_INPUT), module, M::REGEN_INPUT));
	}
};

Model* modelTaleaIn = createModel<TaleaIn, TaleaInWidget>("TaleaIn");
