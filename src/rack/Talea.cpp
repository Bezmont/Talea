#include "plugin.hpp"
#include "Widgets.hpp"
#include "PanelLayout.hpp"
#include "Expander.hpp"
#include "MidiOut.hpp"
#include "core/Engine.hpp"
#include "core/State.hpp"

using namespace talea;

namespace {

constexpr int kNumPads = 16;
constexpr int kNumModes = 6;
constexpr int kNumStepPages = 4;
constexpr int kNumMods = 4;

/** MODE_TRACK is retired (the track-select row replaced it); its value and param stay so
saved patches keep their param indices and pad modes. */
enum PadMode { MODE_TRACK, MODE_STEP, MODE_CYCLE, MODE_KEYS, MODE_MUTE, MODE_PATTERN };

const char* const kPageNames[] = {"TRACK", "STEP", "CYCLE", "KEYS", "SCALE", "PHRASE", "STYLE",
                                  "REPEAT", "GROOVE", "RANDOM", "PATTERN", "SONG", "SETUP"};
constexpr int kNumPages = countOf(kPageNames);
constexpr int kStepPage = 1;
constexpr int kCyclePage = 2;
constexpr int kKeysPage = 3;
constexpr int kScalePage = 4;
constexpr int kPhrasePage = 5;
constexpr int kStylePage = 6;
constexpr int kRepeatPage = 7;
constexpr int kGroovePage = 8;
constexpr int kRandomPage = 9;
constexpr int kPatternPage = 10;
constexpr int kSongPage = 11;
constexpr int kSetupPage = 12;
/** SONG page: the song grid shows pages of 16 entries, 8 to a row. */
constexpr int kSongPageSize = 16;
constexpr int kSongColumns = 8;
/** KEYS page keyboard: 4 octaves from C0 + 12 x keysOctave (C2 by default), up to B8. */
constexpr int kKeysOctaves = 4;
constexpr int kKeysOctaveMax = 5;

/** Short knob names for the CYCLE page's override list. */
const char* const kParamShort[kNumTrackParams] = {"STEPS", "PULSE", "ROT", "DIV", "LEN", "REP", "RAMP", "TIME", "PACE",
                                                  "VEL", "PROB", "SUS", "ACC", "TIM", "DLY", "PITCH", "VOIC", "RANGE"};

const char* const kGrooveShort[kNumGrooves] = {"AGOGO", "TIMBAL", "CONGAS", "BONGO", "SAW", "TRI", "SINE", "PULSE"};

const int kPpqnChoices[] = {1, 2, 4, 8, 12, 24, 48, 96};
constexpr int kNumPpqnChoices = countOf(kPpqnChoices);

std::vector<std::string> names(const NamedValue* values, int count) {
	std::vector<std::string> out;
	for (int i = 0; i < count; i++)
		out.push_back(values[i].name);
	return out;
}

struct RepeatsQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		if (getValue() >= getMaxValue())
			return "∞";
		return ParamQuantity::getDisplayValueString();
	}
};

} // namespace

struct Talea : Module {
	enum ParamId {
		PLAY_PARAM,
		RESET_PARAM,
		TAP_PARAM,
		TEMPO_PARAM,
		TEMP_PARAM,
		ALL_PARAM,
		RND_ASSIGN_PARAM,
		DATA_PARAM,  // retired (v0.2 panel), kept so saved param indices hold
		ENTER_PARAM, // retired
		// Shape
		STEPS_PARAM,
		PULSES_PARAM,
		ROTATE_PARAM,
		DIVISION_PARAM,
		LENGTH_PARAM,
		// Repeats
		REPEATS_PARAM,
		RAMP_PARAM,
		REPEAT_TIME_PARAM,
		PACE_PARAM,
		// Groove
		VELOCITY_PARAM,
		PROBABILITY_PARAM,
		SUSTAIN_PARAM,
		ACCENT_PARAM,
		TIMING_PARAM,
		DELAY_PARAM,
		// Tonal
		PITCH_PARAM,
		HARMONY_PARAM,
		VOICING_PARAM,
		RANGE_PARAM,
		// Random
		VARIANCE_PARAM,
		RANDOM_RATE_PARAM,
		// Pads and edit actions
		MODE_TRACK_PARAM, // retired, not on the panel
		MODE_STEP_PARAM,
		MODE_CYCLE_PARAM,
		MODE_KEYS_PARAM,
		MODE_MUTE_PARAM,
		MODE_PATTERN_PARAM,
		CYCLE_EDIT_PARAM,
		COPY_PARAM,
		PASTE_PARAM,
		CLEAR_PARAM,
		ENUMS(PAD_PARAM, kNumPads),
		ENUMS(STEP_PAGE_PARAM, kNumStepPages),
		FOLLOW_PARAM,
		ENUMS(MOD_ATTEN_PARAM, kNumMods), // retired: CV moved to the IN expander
		ENUMS(TRACK_SELECT_PARAM, kNumTracks),
		PARAMS_LEN
	};
	enum InputId {
		CLOCK_INPUT,
		RESET_INPUT,
		RUN_INPUT,
		// Retired (v0.2): these moved to the IN expander; kept so cable ids of later jacks hold.
		TEMP_INPUT,
		ENUMS(MOD_INPUT, kNumMods),
		TRANSPOSE_INPUT,
		PHRASE_CV_INPUT,
		PATTERN_INPUT,
		MUTE_APPLY_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		PITCH_OUTPUT,
		GATE_OUTPUT,
		VELOCITY_OUTPUT,
		CLOCK_OUTPUT,
		RESET_OUTPUT,
		EOP_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		PLAY_LIGHT,
		CLOCK_LIGHT,
		TEMP_LIGHT,
		ALL_LIGHT,
		RND_ASSIGN_LIGHT,
		ENUMS(MODE_LIGHT, kNumModes),
		CYCLE_EDIT_LIGHT,
		ENUMS(COPY_LIGHT, 2),
		ENUMS(PASTE_LIGHT, 2),
		ENUMS(CLEAR_LIGHT, 2),
		ENUMS(PAD_LIGHT, kNumPads * 3),
		ENUMS(STEP_PAGE_LIGHT, kNumStepPages),
		FOLLOW_LIGHT,
		ENUMS(MOD_LIGHT, kNumMods),
		ENUMS(TRACK_SELECT_LIGHT, kNumTracks * 3),
		LIGHTS_LEN
	};

	/** Rack knob for each per-track parameter of the core table. */
	static int knobOf(int tp) {
		static const int ids[kNumTrackParams] = {
			STEPS_PARAM, PULSES_PARAM, ROTATE_PARAM, DIVISION_PARAM, LENGTH_PARAM,
			REPEATS_PARAM, RAMP_PARAM, REPEAT_TIME_PARAM, PACE_PARAM,
			VELOCITY_PARAM, PROBABILITY_PARAM, SUSTAIN_PARAM, ACCENT_PARAM, TIMING_PARAM, DELAY_PARAM,
			PITCH_PARAM, VOICING_PARAM, RANGE_PARAM,
		};
		return ids[tp];
	}

	talea::Engine seq;
	EventList events;

	// UI state (core rule 8: "selected track" never enters the engine)
	int selectedTrack = 0;
	int padMode = MODE_STEP;
	/** Pad mode before MUTE mode, where a page tab click returns (leaveMute). */
	int preMuteMode = MODE_STEP;
	std::atomic<bool> leaveMuteRequest{false}; // page tab clicked in MUTE mode (UI thread)
	int stepPage = 0;
	bool follow = true;
	bool allTracks = false;
	bool rndAssign = false;
	bool cycleEdit = false;
	int selectedCycle = 0;
	/** CYCLE page clicks, run on the audio thread so knobs never write stale values into a
	cycle: action << 8 | argument, 0 = none. */
	enum CycleAction { CYCLE_SELECT = 1, CYCLE_COUNT, CYCLE_CLEAR_ALL, CYCLE_CLEAR_PARAM, CYCLE_CLEAR_STEPS, CYCLE_CLEAR_RANDOM, CYCLE_CLEAR_LOCKS, CYCLE_CLEAR_TONAL };
	std::atomic<int> cycleAction{0};
	int keysOctave = 2;      // KEYS page: the keyboard starts at C0 + 12 x this (C2 by default)
	float harmonyLast = 0.f; // Harmony knob position already applied, in detents
	/** The pool each track had before the current run of Harmony detents, for the knob's
	*Restore pool*: Harmony can't be undone by turning back. Not saved with the patch. */
	talea::HarmonyUndo harmonyUndo;
	std::atomic<bool> harmonyRestoreRequest{false}; // from the Harmony knob's menu (UI thread)
	double uiClock = 0.0;    // seconds, for blinking lights
	std::atomic<bool> padCtrl[kNumPads] = {}; // Ctrl held on the latest click of each pad
	std::atomic<bool> padShift[kNumPads] = {};
	std::atomic<bool> trackCtrl[kNumTracks] = {}, trackShift[kNumTracks] = {};
	/** Tracks grouped with the selected one (Ctrl/Shift+click track buttons): plain knob
	turns move them by the same amount. All groups every track. */
	uint16_t trackGroup = 0;
	/** PATTERN page clicks and menu actions, run on the audio thread: action << 8 | slot. */
	enum PatternAction { PATTERN_QUEUE = 1, PATTERN_COPY, PATTERN_CLEAR, PATTERN_CLEAR_CHAIN };
	std::atomic<int> patternAction{0};
	uint32_t lastSwitches = 0;
	uint32_t lastRestarts = 0;
	/** SONG page clicks, run on the audio thread: action << 24 | entry << 16 | value. */
	enum SongAction { SONG_TOGGLE = 1, SONG_LOOP, SONG_ADD, SONG_INSERT, SONG_DELETE, SONG_CLEAR, SONG_SLOT, SONG_BARS, SONG_BARS_PATTERN };
	static constexpr int kSongPlayingSlot = 0xffff; // SONG_ADD value: the playing pattern
	std::atomic<uint32_t> songAction{0};
	int songSel = 0;                         // SONG page: selected entry (UI)
	int songPage = 0;                        // SONG page: page of 16 entries shown (UI)
	bool songFollow = true;                  // SONG page: the page follows the playing entry
	std::atomic<bool> songPageShown{false};  // the display shows the SONG page: pattern pads add entries
	bool songEndHold = false;                // the song ended: a Run gate held high doesn't restart it
	void requestSong(int action, int entry = 0, int value = 0) {
		songAction = uint32_t(action) << 24 | uint32_t(entry & 0xff) << 16 | uint32_t(value & 0xffff);
	}
	/** COPY / PASTE / CLEAR: one is armed at a time and the next clicks on track buttons,
	knobs, CYCLE pads and PATTERN pads act on the clipboard instead. */
	enum Armed { ARM_NONE, ARM_COPY, ARM_PASTE, ARM_CLEAR };
	int armed = ARM_NONE;
	bool clearSticky = false;           // Ctrl+click CLEAR: stays armed
	std::atomic<bool> clearCtrl{false}; // Ctrl held on the latest CLEAR click
	std::atomic<int> knobClick{-1};     // param id of a knob clicked while armed (UI thread)
	enum ClipKind { CLIP_NONE, CLIP_TRACK, CLIP_CYCLE, CLIP_PATTERN, CLIP_PARAM, CLIP_STEP };
	struct Clipboard {
		int kind = CLIP_NONE;
		int source = 0; // track, cycle or pattern slot it came from, for the display
		Track track;
		TrackCycles cycles;
		TrackRandom random;
		CycleOverrides cycle;
		Pattern pattern;
		int param = 0;
		float value = 0.f;
		PoolLock stepPool;
		bool stepHit = false;
		uint32_t stepMask = 0;
		float stepValues[kNumTrackParams] = {};
	} clip;
	/** STEP mode lock selection (Ctrl+click pads): unrotated step positions of the selected
	track. While set, knobs of lockable parameters edit these steps' locks. */
	StepMask lockSel = 0;
	std::atomic<bool> randomLockRequest{false}; // RANDOM page LOCK click (UI thread)
	/** Live settings of the selected track (playing cycle, step locks, random, CV), refreshed
	with the lights, for the knob rings and the display. */
	float liveValue[kNumTrackParams] = {};
	/** The knob values at the same moment: a dragged knob moves every sample (Rack's smoothing),
	so comparing the live values with the current ones would read a drag as modulation. */
	float liveShown[kNumTrackParams] = {};

	/** The knob's parameter plays at another value right now than the knob shows (random or
	CV, or the playing cycle / step when not editing them). Not under Rnd assign, where the
	knobs show amounts. */
	bool liveDiffers(int tp, float& live) {
		if (rndAssign)
			return false;
		live = liveValue[tp];
		return std::fabs(live - liveShown[tp]) > 1e-3f;
	}

	/** Display follows focus: touching a knob shows its page (context menu). */
	bool displayFollows = true;
	std::atomic<int> focusPage{-1}; // page asked for by a knob touch, taken by the display
	std::atomic<int> keyRequest{-1}; // KEYS page key click (UI thread): the MIDI note, or kKeyTrackPool
	static constexpr int kKeyTrackPool = -2; // TRACK POOL: drop the selected steps' pool locks
	/** STEP page lane edits from the display, applied on the audio thread so knobs never
	write a stale value back: per step, valid << 63 | remove << 62 | param << 32 | value
	bits. */
	std::atomic<uint64_t> laneEdits[kMaxSteps] = {};
	std::atomic<bool> laneEditPending{false};

	void requestLaneEdit(int pos, int tp, float v, bool remove) {
		uint32_t bits;
		std::memcpy(&bits, &v, sizeof bits);
		laneEdits[pos] = uint64_t(1) << 63 | uint64_t(remove) << 62 | uint64_t(tp) << 32 | bits;
		laneEditPending = true;
	}

	void applyLaneEdits() {
		if (!laneEditPending.exchange(false))
			return;
		for (int pos = 0; pos < kMaxSteps; pos++) {
			uint64_t e = laneEdits[pos].exchange(0);
			if (!(e >> 63))
				continue;
			int tp = int((e >> 32) & 0xff);
			uint32_t bits = uint32_t(e);
			float v;
			std::memcpy(&v, &bits, sizeof v);
			if ((e >> 62) & 1)
				clearLock(pos, tp);
			else
				setLock(pos, tp, v);
		}
		loadKnobs();
	}

	/** Page a knob belongs to (display follows focus), or -1. With steps selected the lockable
	knobs show the STEP page's lock lanes; under Rnd assign the randomisable ones RANDOM. */
	int pageOfKnob(int paramId) {
		if (paramId == VARIANCE_PARAM || paramId == RANDOM_RATE_PARAM)
			return kRandomPage;
		if (paramId == HARMONY_PARAM)
			return kScalePage;
		for (int tp = 0; tp < kNumTrackParams; tp++) {
			if (knobOf(tp) != paramId)
				continue;
			if (rndAssign && randomisable(TrackParam(tp)))
				return kRandomPage;
			if (lockSel && lockable(TrackParam(tp)))
				return kStepPage;
			switch (tp) {
				case TP_REPEATS:
				case TP_RAMP:
				case TP_REPEAT_TIME:
				case TP_PACE: return kRepeatPage;
				case TP_VELOCITY:
				case TP_PROBABILITY:
				case TP_SUSTAIN:
				case TP_ACCENT:
				case TP_TIMING:
				case TP_DELAY: return kGroovePage;
				case TP_PITCH: return kScalePage;
				case TP_VOICING: return kStylePage;
				case TP_RANGE: return kPhrasePage;
				default: return 0; // shape knobs: TRACK
			}
		}
		return -1;
	}
	int lastPatternIn = -1; // slot the Pattern input last selected; -1 = unplugged
	enum MuteRequest { MUTE_REQUEST_NONE, MUTE_REQUEST_APPLY, MUTE_REQUEST_CLEAR };
	std::atomic<int> muteRequest{MUTE_REQUEST_NONE}; // from the MUTE button's menu (UI thread)
	/** Temp: the button latches it, the gate input holds it. */
	bool tempLatched = false;
	std::atomic<bool> tempCtrl{false}; // Ctrl held on the latest Temp click
	enum TempRequest { TEMP_REQUEST_NONE, TEMP_REQUEST_KEEP, TEMP_REQUEST_DISCARD };
	std::atomic<int> tempRequest{TEMP_REQUEST_NONE}; // from the Temp button's menu

	// Transport
	bool playing = false;
	double beat = 0.0;     // musical time in quarter notes since reset
	double lastBeat = 0.0; // end of the last processed window
	int ppqnInIndex = 2;   // 4 PPQN
	int ppqnOutIndex = 2;
	bool runGateMode = true; // Run input: gate = run while high; else trigger toggles
	long clockOutCount = -1;
	long eopCount = 0;
	std::atomic<bool> killRequested{false};

	// External clock
	long extPulses = -1; // pulses since reset; -1 = waiting for the first
	double extPulseBeat = 0.0;
	int64_t samplesSincePulse = 0;
	double periodSamples = 0.0;
	float displayBpm = 120.f;

	// Tap tempo
	int64_t lastTapFrame = -1;
	float tapIntervals[4] = {};
	int tapCount = 0;

	// Outputs per track
	bool gate[kNumTracks] = {};
	float pitchV[kNumTracks] = {};
	float velocityV[kNumTracks] = {};
	float retrig[kNumTracks] = {}; // seconds of forced-low gate after a retrigger
	MidiNoteOut midi;
	/** The latest note-on's chord per track, for the OUT expander (held between gates). */
	float chordV[kNumTracks][kMaxPool] = {};
	int chordN[kNumTracks];
	/** CC track lanes: the value (0-127) glides from `from` to `to` over `glide` quarter notes
	from `start`. The lane drives Pitch (0-10 V) and MIDI CC; Velocity is the target. */
	struct CcLane {
		bool on = false; // the track's latest note-on was a CC value
		int number = kDefaultCc;
		float from = 0.f, to = 0.f, value = 0.f;
		double start = 0.0, glide = 0.0;
	};
	CcLane ccLane[kNumTracks];
	dsp::ClockDivider ccDivider; // MIDI CC at most ~1 kHz

	dsp::BooleanTrigger playTrigger, resetTrigger, tapTrigger, allTrigger, rndTrigger, cycleEditTrigger, followTrigger;
	dsp::BooleanTrigger modeTriggers[kNumModes];
	dsp::BooleanTrigger pageTriggers[kNumStepPages];
	dsp::BooleanTrigger padTriggers[kNumPads];
	dsp::BooleanTrigger trackTriggers[kNumTracks];
	dsp::BooleanTrigger tempTrigger, copyTrigger, pasteTrigger, clearTrigger;
	dsp::SchmittTrigger clockInTrigger, resetInTrigger, runInTrigger, muteApplyTrigger, tempInTrigger;
	// IN expander mute gates and Random regenerate triggers, per track
	dsp::SchmittTrigger muteGate[kNumTracks], regenTrigger[kNumTracks];
	bool muteGateHigh[kNumTracks] = {};
	InMessage inMessages[2];
	dsp::PulseGenerator clockOutPulse, resetOutPulse, eopPulse;
	dsp::ClockDivider lightDivider;

	// Undo (UI thread only). Most edits run on the audio thread, so Rack's history doesn't see
	// them: a press on the panel snapshots the module, and once the press has been handled
	// (a few frames on, after any menu it opened has closed) the change goes into the
	// history as one step, if the patch data changed and nothing else (a knob turn,
	// Initialize, an undo) went into the history meanwhile.
	json_t* undoBefore = nullptr;
	std::string undoKey;
	int undoFrames = 0;
	int undoIndex = 0;
	history::Action* undoTop = nullptr;

	static history::Action* historyTop() {
		history::State* h = APP->history;
		return h->actionIndex > 0 && h->actionIndex <= int(h->actions.size()) ? h->actions[h->actionIndex - 1] : nullptr;
	}

	/** The saved data without UI state (selections, pad mode), for telling edits apart. */
	static std::string undoDataKey(json_t* moduleJ) {
		json_t* data = json_deep_copy(json_object_get(moduleJ, "data"));
		if (!data)
			return "";
		for (const char* key : {"selectedTrack", "selectedCycle", "padMode", "follow", "displayFollows", "keysOctave", "trackGroup", "allTracks",
		                        "songFollow"})
			json_object_del(data, key);
		char* text = json_dumps(data, JSON_COMPACT | JSON_SORT_KEYS);
		std::string key = text ? text : "";
		free(text);
		json_decref(data);
		return key;
	}

	void undoBegin() {
		undoFinish();
		undoBefore = APP->engine->moduleToJson(this);
		undoKey = undoDataKey(undoBefore);
		undoFrames = 6;
		undoIndex = APP->history->actionIndex;
		undoTop = historyTop();
	}

	void undoCancel() {
		if (undoBefore)
			json_decref(undoBefore);
		undoBefore = nullptr;
	}

	void undoFinish() {
		if (!undoBefore)
			return;
		json_t* after = APP->engine->moduleToJson(this);
		bool historyMoved = APP->history->actionIndex != undoIndex || historyTop() != undoTop;
		if (!historyMoved && undoDataKey(after) != undoKey) {
			history::ModuleChange* h = new history::ModuleChange;
			h->name = "Talea edit";
			h->moduleId = id;
			h->oldModuleJ = undoBefore;
			h->newModuleJ = after;
			APP->history->push(h);
		}
		else {
			json_decref(undoBefore);
			json_decref(after);
		}
		undoBefore = nullptr;
	}

	/** Each UI frame. */
	void undoStep() {
		if (!undoBefore || APP->event->draggedWidget || APP->scene->getFirstDescendantOfType<ui::MenuOverlay>())
			return;
		if (--undoFrames <= 0)
			undoFinish();
	}

	~Talea() {
		undoCancel();
	}

	Talea() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configButton(PLAY_PARAM, "Play / pause");
		configButton(RESET_PARAM, "Reset");
		configButton(TAP_PARAM, "Tap tempo");
		configParam(TEMPO_PARAM, 24.f, 280.f, 120.f, "Tempo", " BPM");
		paramQuantities[TEMPO_PARAM]->snapEnabled = true;
		configButton(TEMP_PARAM, "Temp (click: on / off and revert; Ctrl+click: keep changes)");
		configButton(ALL_PARAM, "All tracks (knob turns move every track by the same amount)");
		configButton(RND_ASSIGN_PARAM, "Random assign");
		configParam(DATA_PARAM, -kEndlessRange, kEndlessRange, 0.f, "Data");
		paramQuantities[DATA_PARAM]->snapEnabled = true;
		paramQuantities[DATA_PARAM]->resetEnabled = false;
		configButton(ENTER_PARAM, "Enter");

		for (int tp = 0; tp < kNumTrackParams; tp++) {
			const ParamInfo& info = kTrackParams[tp];
			int id = knobOf(tp);
			if (info.list)
				configSwitch(id, info.min, info.max, info.def, info.name, names(info.list, info.listSize));
			else if (tp == TP_REPEATS)
				configParam<RepeatsQuantity>(id, info.min, info.max, info.def, info.name, info.unit);
			else
				configParam(id, info.min, info.max, info.def, info.name, info.unit);
			paramQuantities[id]->snapEnabled = info.snap;
		}
		configParam(HARMONY_PARAM, -kEndlessRange, kEndlessRange, 0.f, "Harmony");
		paramQuantities[HARMONY_PARAM]->snapEnabled = true;
		paramQuantities[HARMONY_PARAM]->resetEnabled = false;

		configParam(VARIANCE_PARAM, 0.f, 100.f, 0.f, "Variance", "%");
		configSwitch(RANDOM_RATE_PARAM, 0.f, float(countOf(kRandomRates) - 1), float(kDefaultRandomRate), "Random rate", names(kRandomRates, countOf(kRandomRates)));

		const char* modeNames[kNumModes] = {"Track", "Step", "Cycle", "Keys (retired)", "Mute", "Pattern"};
		for (int i = 0; i < kNumModes; i++)
			configButton(MODE_TRACK_PARAM + i, string::f("Pad mode: %s", modeNames[i]));
		configButton(CYCLE_EDIT_PARAM, "Cycle edit");
		configButton(COPY_PARAM, "Copy (then click a track button, knob, cycle or pattern pad)");
		configButton(PASTE_PARAM, "Paste (every target clicked receives the clipboard; click again to stop)");
		configButton(CLEAR_PARAM, "Clear (then click a target; Ctrl+click: stays armed until clicked again)");
		for (int i = 0; i < kNumPads; i++)
			configButton(PAD_PARAM + i, string::f("Pad %d", i + 1));
		for (int i = 0; i < kNumStepPages; i++)
			configButton(STEP_PAGE_PARAM + i, string::f("Step page %d (steps %d-%d)", i + 1, i * 16 + 1, i * 16 + 16));
		configButton(FOLLOW_PARAM, "Follow playhead");
		for (int i = 0; i < kNumTracks; i++)
			configButton(TRACK_SELECT_PARAM + i, string::f("Track %d (click: select; Ctrl: group; Shift: group range)", i + 1));
		for (int i = 0; i < kNumMods; i++)
			configParam(MOD_ATTEN_PARAM + i, -1.f, 1.f, 0.f, string::f("Mod %d amount", i + 1), "%", 0.f, 100.f);

		configInput(CLOCK_INPUT, "Clock");
		configInput(RESET_INPUT, "Reset");
		configInput(RUN_INPUT, "Run");
		configInput(TEMP_INPUT, "Temp gate (Temp while high)");
		for (int i = 0; i < kNumMods; i++)
			configInput(MOD_INPUT + i, string::f("Mod %d", i + 1));
		configInput(TRANSPOSE_INPUT, "Transpose (V/oct, channel n = track n)");
		configInput(PHRASE_CV_INPUT, "Phrase CV (channel n = track n)");
		configInput(PATTERN_INPUT, "Pattern select (0-10 V)");
		configInput(MUTE_APPLY_INPUT, "Mute apply (trigger applies staged mutes)");

		configOutput(PITCH_OUTPUT, "Pitch (V/oct, channel n = track n)");
		configOutput(GATE_OUTPUT, "Gate (channel n = track n)");
		configOutput(VELOCITY_OUTPUT, "Velocity (0-10 V, channel n = track n)");
		configOutput(CLOCK_OUTPUT, "Clock");
		configOutput(RESET_OUTPUT, "Reset");
		configOutput(EOP_OUTPUT, "End of pattern");

		lightDivider.setDivision(64);
		leftExpander.producerMessage = &inMessages[0];
		leftExpander.consumerMessage = &inMessages[1];
		for (int& n : chordN)
			n = 1;
		rewind();
	}

	// ------------------------------------------------------------ helpers

	Track& track() {
		return seq.pattern.tracks[selectedTrack];
	}

	TrackCycles& cycles() {
		return seq.pattern.cycles[selectedTrack];
	}

	/** The selected track as it plays in the cycle being edited (or itself), for display. */
	Track viewTrack() {
		return cycleEdit ? cycles().resolve(track(), selectedCycle) : track();
	}

	TrackRandom& random() {
		return seq.pattern.random[selectedTrack];
	}

	StepLocks& locks() {
		return seq.pattern.locks[selectedTrack];
	}

	/** The knob edits the selected steps' locks (Rnd assign keeps editing random amounts). */
	bool lockEditing(int tp) {
		return lockSel && !rndAssign && lockable(TrackParam(tp));
	}

	int firstLockStep() {
		for (int s = 0; s < kMaxSteps; s++) {
			if (hasHit(lockSel, s))
				return s;
		}
		return 0;
	}

	/** Where scale / root / pool edits go: the edited cycle's own (made from the track's on
	the first edit) in Cycle edit, else the track. */
	Track& tonalTarget() {
		return cycleEdit ? cycles().tonalEdit(track(), selectedCycle) : track();
	}

	/** Which pool of track `t` Harmony edits: the edited cycle's for the selected track in
	Cycle edit, else the track's own. Only the selected track follows Cycle edit; the rest of
	a group is edited at track level. */
	int harmonySlot(int t) const {
		return t == selectedTrack && cycleEdit ? selectedCycle : -1;
	}

	/** Whether *Restore pool* has anything to put back for the current group. */
	bool canRestorePool() {
		uint16_t group = groupMask();
		for (int t = 0; t < kNumTracks; t++) {
			if ((group & (1u << t)) && harmonyUndo.has(t, harmonySlot(t)))
				return true;
		}
		return false;
	}

	/** Step `pos`'s pool lock as the edited cycle (or the track) plays it, or nullptr. */
	const PoolLock* poolLockAt(int pos) {
		if (cycleEdit) {
			if (const PoolLock* p = cycleLocks().poolAt(pos))
				return p;
		}
		return locks().poolAt(pos);
	}

	/** The pool the first selected step plays: its pool lock, else the track's (the edited
	cycle's in Cycle edit). */
	Track stepPoolTrack() {
		Track t = viewTrack();
		if (const PoolLock* p = poolLockAt(firstLockStep()))
			p->apply(t);
		return t;
	}

	/** KEYS page with steps selected: toggles `note` in the first selected step's pool (the
	track's pool to start from) and gives every selected step the result. Pool locks go to
	the edited cycle in Cycle edit, like the other locks. */
	void toggleStepPoolNote(int note) {
		Track t = stepPoolTrack();
		if (!t.inPool(note) && !inScale(note, t.scaleMask(), t.root))
			return;
		if (!t.togglePoolNote(note))
			return;
		for (int pos = 0; pos < kMaxSteps; pos++) {
			if (!hasHit(lockSel, pos))
				continue;
			if (cycleEdit)
				cycleLocks().setPool(pos, t.pool, t.poolSize);
			else
				locks().setPool(pos, t.pool, t.poolSize);
		}
	}

	/** TRACK POOL: the selected steps follow the track's (or cycle's) pool again. */
	void clearStepPools() {
		for (int pos = 0; pos < kMaxSteps; pos++) {
			if (!hasHit(lockSel, pos))
				continue;
			if (cycleEdit)
				cycleLocks().setPool(pos, nullptr, 0);
			else
				locks().setPool(pos, nullptr, 0);
		}
	}

	CycleLocks& cycleLocks() {
		return cycles().cycles[selectedCycle].locks;
	}

	/** Step `pos` has locks that play in cycle `c` (the track's or the cycle's own); c < 0 =
	what step edits reach: the edited cycle in Cycle edit, else the track. */
	bool locksAt(int pos, int c = -1) {
		if (c < 0)
			c = cycleEdit ? selectedCycle : -1;
		return locks().any(pos) || (c >= 0 && cycles().cycles[c].locks.any(pos));
	}

	/** A step's lock of `p` as the edited cycle (or the track) plays it. */
	bool lockValue(int pos, int tp, float& v) {
		if (cycleEdit && cycleLocks().has(pos, TrackParam(tp))) {
			v = cycleLocks().get(pos, TrackParam(tp));
			return true;
		}
		if (locks().has(pos, TrackParam(tp))) {
			v = locks().values[pos][tp];
			return true;
		}
		return false;
	}

	/** Lock edits go to the edited cycle in Cycle edit, else to the track. */
	void setLock(int pos, int tp, float v) {
		if (cycleEdit)
			cycleLocks().set(pos, TrackParam(tp), v);
		else
			locks().set(pos, TrackParam(tp), v);
	}

	void clearLock(int pos, int tp) {
		if (cycleEdit)
			cycleLocks().clear(pos, TrackParam(tp));
		else
			locks().clear(pos, TrackParam(tp));
	}

	void clearLockSelection() {
		if (!lockSel)
			return;
		lockSel = 0;
		loadKnobs();
	}

	/** Rnd assign maps a random amount (-100..100 %) onto the knob's travel, 0 = centre. */
	static float amountToKnob(int tp, float amount) {
		const ParamInfo& info = kTrackParams[tp];
		return info.min + (amount + 100.f) / 200.f * (info.max - info.min);
	}

	static float knobToAmount(int tp, float v) {
		const ParamInfo& info = kTrackParams[tp];
		return std::round((v - info.min) / (info.max - info.min) * 200.f - 100.f);
	}

	/** The knob sets a random amount rather than a value. */
	bool assigning(int tp) {
		return rndAssign && randomisable(TrackParam(tp));
	}

	/** Random amount being edited: the edited cycle's while Cycle edit is on, else the
	track's. */
	float randomAmount(int tp) {
		return cycleEdit ? cycles().randomAmount(random(), selectedCycle, TrackParam(tp)) : random().amount[tp];
	}

	void setRandomAmount(int tp, float amount) {
		if (cycleEdit)
			cycles().setRandom(selectedCycle, TrackParam(tp), amount);
		else
			random().amount[tp] = amount;
	}

	/** Value a knob shows: its random amount under Rnd assign, the first selected step's lock
	while steps are selected, the edited cycle's value while Cycle edit is on, else the
	track's. */
	float knobValue(int tp) {
		if (assigning(tp))
			return amountToKnob(tp, randomAmount(tp));
		float locked;
		if (lockEditing(tp) && lockValue(firstLockStep(), tp, locked))
			return locked;
		return cycleEdit ? cycles().value(track(), selectedCycle, TrackParam(tp)) : track().values[tp];
	}

	/** Under Rnd assign the knobs of parameters that can't be randomised are locked, and
	while steps are selected those that can't be step-locked. */
	bool knobLocked(int paramId) {
		if (!rndAssign && !lockSel)
			return false;
		for (int tp = 0; tp < kNumTrackParams; tp++) {
			if (knobOf(tp) == paramId)
				return rndAssign ? !randomisable(TrackParam(tp)) : !lockable(TrackParam(tp));
		}
		return false;
	}

	/** Clears a random amount from the UI thread: the knob moves first, so the audio thread
	never writes the old amount back. */
	void clearRandom(int tp) {
		if (assigning(tp))
			params[knobOf(tp)].setValue(amountToKnob(tp, 0.f));
		setRandomAmount(tp, 0.f);
	}

	/** Ends Temp, reverting or keeping its edits. Mutes staged during it apply now. */
	void endTemp(bool keep) {
		seq.endTemp(keep);
		tempLatched = false;
		selectedCycle = std::min(selectedCycle, cycles().count - 1);
		seq.commitMutes(lastBeat);
		loadKnobs();
	}

	/** After the playing pattern changed (a switch or a slot write): knobs and selections
	follow the new pattern. Temp has ended. */
	void patternChanged() {
		tempLatched = false;
		selectedCycle = std::min(selectedCycle, cycles().count - 1);
		loadKnobs();
	}

	/** Pad / page / input selection of a pattern: a click queues it, Shift adds it to the chain,
	Ctrl switches at once. Stopped, everything switches at once. */
	void selectPattern(int slot, bool chain, bool now) {
		now |= !playing;
		if (chain)
			seq.chainPattern(slot, lastBeat, events, now);
		else
			seq.queuePattern(slot, lastBeat, events, now);
		applyEvents();
	}

	void applyPatternAction() {
		int a = patternAction.exchange(0);
		int slot = a & 0xff;
		switch (a >> 8) {
			case PATTERN_QUEUE: selectPattern(slot, false, false); break;
			case PATTERN_COPY:
				// Copies what plays, Temp edits included; onto the playing slot that keeps them.
				if (slot != seq.currentPattern())
					seq.storePattern(slot, seq.pattern);
				else if (seq.tempActive())
					endTemp(true);
				break;
			case PATTERN_CLEAR:
				seq.clearPattern(slot);
				if (slot == seq.currentPattern())
					patternChanged();
				break;
			case PATTERN_CLEAR_CHAIN: seq.clearChain(); break;
			default: break;
		}
	}

	/** Song edits (audio thread). Turning the song on starts it at once after a reset,
	otherwise on the next bar line; clicking it while overridden resumes it. */
	void applySongAction() {
		uint32_t a = songAction.exchange(0);
		if (!a)
			return;
		int action = int(a >> 24), entry = int(a >> 16 & 0xff), value = int(a & 0xffff);
		talea::Song& song = seq.song;
		bool valid = entry < song.count;
		switch (action) {
			case SONG_TOGGLE:
				if (!seq.songOn())
					seq.setSongOn(true, lastBeat, events);
				else if (seq.songOverridden() && !seq.songPending())
					seq.resumeSong(lastBeat);
				else
					seq.setSongOn(false, lastBeat, events);
				break;
			case SONG_LOOP: song.loop = !song.loop; break;
			case SONG_ADD: {
				int slot = value == kSongPlayingSlot ? seq.currentPattern() : clamp(value, 0, kNumPatterns - 1);
				seq.songInsert(song.count, {slot, seq.defaultBars(slot)}, lastBeat);
				break;
			}
			case SONG_INSERT:
				if (valid)
					seq.songInsert(entry, song.entries[entry], lastBeat);
				break;
			case SONG_DELETE:
				if (valid)
					seq.songRemove(entry, lastBeat);
				break;
			case SONG_CLEAR: seq.songClear(); break;
			case SONG_SLOT:
				// A new pattern for the entry: its bars follow the pattern's length.
				if (valid)
					song.entries[entry] = {clamp(value, 0, kNumPatterns - 1), seq.defaultBars(value)};
				break;
			case SONG_BARS:
				if (valid)
					song.entries[entry].bars = clamp(value, 1, talea::kMaxSongBars);
				break;
			case SONG_BARS_PATTERN:
				if (valid)
					song.entries[entry].bars = seq.defaultBars(song.entries[entry].slot);
				break;
			default: break;
		}
		applyEvents();
	}

	/** After an edit replaced the selected track's data. */
	void trackChanged() {
		selectedCycle = std::min(selectedCycle, cycles().count - 1);
		loadKnobs();
	}

	void clearDone() {
		if (!clearSticky)
			armed = ARM_NONE;
	}

	/** A track button click while armed; false = not a clipboard target (it selects). */
	bool clipTrack(int t) {
		Pattern& pat = seq.pattern;
		switch (armed) {
			case ARM_COPY:
				clip.kind = CLIP_TRACK;
				clip.source = t;
				clip.track = pat.tracks[t];
				clip.cycles = pat.cycles[t];
				clip.random = pat.random[t];
				armed = ARM_NONE;
				return true;
			case ARM_PASTE:
				if (clip.kind == CLIP_TRACK) {
					pat.tracks[t] = clip.track;
					pat.cycles[t] = clip.cycles;
					pat.random[t] = clip.random;
				}
				else if (clip.kind == CLIP_PARAM)
					pat.setBase(t, TrackParam(clip.param), clip.value);
				else
					return false;
				break;
			case ARM_CLEAR:
				pat.tracks[t].reset();
				pat.cycles[t].reset();
				pat.random[t].reset(t);
				clearDone();
				break;
			default: return false;
		}
		if (t == selectedTrack)
			trackChanged();
		return true;
	}

	/** A pad click while armed; false = not a clipboard target in this mode (the pad acts as
	usual). */
	bool clipPad(int i) {
		if (armed == ARM_NONE)
			return false;
		if (padMode == MODE_STEP) {
			// A step: its on/off state and locks.
			Track view = viewTrack();
			int step = stepPage * 16 + i;
			if (step >= view.steps())
				return true;
			int pos = view.unrotated(step);
			bool hit = hasHit(seq.hits(selectedTrack, cycleEdit ? selectedCycle : -1), step);
			StepLocks& lk = locks();
			switch (armed) {
				case ARM_COPY:
					// The locks as the step plays here (in Cycle edit the cycle's over the track's).
					clip.kind = CLIP_STEP;
					clip.source = step;
					clip.stepHit = hit;
					clip.stepMask = 0;
					clip.stepPool = poolLockAt(pos) ? *poolLockAt(pos) : PoolLock();
					for (int p = 0; p < kNumTrackParams; p++) {
						float v;
						if (lockValue(pos, p, v)) {
							clip.stepMask |= 1u << p;
							clip.stepValues[p] = v;
						}
					}
					armed = ARM_NONE;
					return true;
				case ARM_PASTE:
					if (clip.kind != CLIP_STEP)
						return true;
					if (hit != clip.stepHit) {
						if (cycleEdit)
							cycles().toggleStep(track(), selectedCycle, step);
						else
							track().toggleStep(step);
					}
					if (cycleEdit) {
						cycleLocks().clearStep(pos);
						for (int p = 0; p < kNumTrackParams; p++) {
							if (clip.stepMask & (1u << p))
								cycleLocks().set(pos, TrackParam(p), clip.stepValues[p]);
						}
						int notes[kMaxPool];
						for (int k = 0; k < clip.stepPool.size; k++)
							notes[k] = clip.stepPool.notes[k];
						cycleLocks().setPool(pos, notes, clip.stepPool.size);
					}
					else
						lk.copyStep(pos, clip.stepMask, clip.stepValues, clip.stepPool);
					break;
				case ARM_CLEAR: {
					// Its locks and its manual on/off edit (the edited cycle's in Cycle edit).
					if (cycleEdit)
						cycleLocks().clearStep(pos);
					else
						lk.clearStep(pos);
					StepMask keep = ~(StepMask(1) << pos);
					lockSel &= keep; // a cleared step leaves the lock selection
					CycleOverrides& o = cycles().cycles[selectedCycle];
					if (cycleEdit && o.ownHits) {
						o.manualHits &= keep;
						o.removedHits &= keep;
					}
					else if (!cycleEdit) {
						track().manualHits &= keep;
						track().removedHits &= keep;
					}
					clearDone();
					break;
				}
				default: break;
			}
			loadKnobs();
			return true;
		}
		if (padMode == MODE_CYCLE) {
			if (i >= kMaxCycles)
				return true;
			TrackCycles& cyc = cycles();
			switch (armed) {
				case ARM_COPY:
					clip.kind = CLIP_CYCLE;
					clip.source = i;
					clip.cycle = cyc.cycles[i];
					armed = ARM_NONE;
					return true;
				case ARM_PASTE:
					if (clip.kind == CLIP_CYCLE)
						cyc.cycles[i] = clip.cycle;
					else if (clip.kind == CLIP_PARAM)
						cyc.set(i, TrackParam(clip.param), clip.value);
					else
						return true;
					cyc.count = std::max(cyc.count, i + 1); // pasting past the end adds cycles
					break;
				case ARM_CLEAR:
					cyc.clearCycle(i);
					clearDone();
					break;
				default: break;
			}
			loadKnobs();
			return true;
		}
		if (padMode == MODE_PATTERN) {
			bool playing = i == seq.currentPattern();
			switch (armed) {
				case ARM_COPY:
					clip.kind = CLIP_PATTERN;
					clip.source = i;
					clip.pattern = playing ? seq.pattern : seq.patternSlot(i); // what plays, Temp included
					armed = ARM_NONE;
					return true;
				case ARM_PASTE:
					if (clip.kind != CLIP_PATTERN)
						return true;
					seq.storePattern(i, clip.pattern);
					break;
				case ARM_CLEAR:
					seq.clearPattern(i);
					clearDone();
					break;
				default: break;
			}
			if (playing)
				patternChanged();
			return true;
		}
		return false;
	}

	/** A knob click while armed: COPY takes its value (the edited cycle's in Cycle edit),
	CLEAR resets it to default and drops its random amount (in Cycle edit: the cycle's
	override and random override). */
	void clipKnob(int paramId) {
		int tp = -1;
		for (int p = 0; p < kNumTrackParams; p++) {
			if (knobOf(p) == paramId)
				tp = p;
		}
		if (tp < 0)
			return;
		if (armed == ARM_COPY) {
			clip.kind = CLIP_PARAM;
			clip.param = tp;
			if (lockEditing(tp))
				clip.value = knobValue(tp);
			else
				clip.value = cycleEdit ? cycles().value(track(), selectedCycle, TrackParam(tp)) : track().values[tp];
			armed = ARM_NONE;
		}
		else if (armed == ARM_CLEAR) {
			if (lockEditing(tp)) {
				// The selected steps' lock of this parameter.
				for (int pos = 0; pos < kMaxSteps; pos++) {
					if (hasHit(lockSel, pos))
						clearLock(pos, tp);
				}
			}
			else if (cycleEdit) {
				cycles().clear(selectedCycle, TrackParam(tp));
				cycles().clearRandom(selectedCycle, TrackParam(tp));
			}
			else {
				seq.pattern.setBase(selectedTrack, TrackParam(tp), kTrackParams[tp].def);
				random().amount[tp] = 0.f;
			}
			clearDone();
			loadKnobs();
		}
	}

	/** What the clipboard holds, for the display. */
	std::string clipName() {
		switch (clip.kind) {
			case CLIP_TRACK: return string::f("TRACK %d", clip.source + 1);
			case CLIP_CYCLE: return string::f("CYCLE %d", clip.source + 1);
			case CLIP_PATTERN: return string::f("PATTERN %d", clip.source + 1);
			case CLIP_STEP: return string::f("STEP %d", clip.source + 1);
			case CLIP_PARAM: {
				const ParamInfo& info = kTrackParams[clip.param];
				std::string v = info.list ? info.list[clamp(int(clip.value), 0, info.listSize - 1)].name
				                          : string::f("%g", clip.value);
				return string::f("%s %s", info.name, v.c_str());
			}
			default: return "NOTHING";
		}
	}

	/** Tracks a plain knob turn edits: the selected one and its group (all with All). */
	uint16_t groupMask() {
		return allTracks ? uint16_t(0xFFFF) : uint16_t(trackGroup | 1u << selectedTrack);
	}

	bool grouped() {
		uint16_t m = groupMask();
		return m & (m - 1); // more than one track
	}

	/** Sets the selected track's value and moves the rest of the group by the same amount,
	each clamped to the range (cycle overrides move with their track). */
	void setGroupBase(int tp, float v) {
		float before = track().values[tp];
		seq.pattern.setBase(selectedTrack, TrackParam(tp), v);
		float delta = track().values[tp] - before;
		uint16_t m = groupMask();
		for (int t = 0; t < kNumTracks; t++) {
			if (t != selectedTrack && (m & (1u << t)) && delta != 0.f)
				seq.pattern.setBase(t, TrackParam(tp), seq.pattern.tracks[t].values[tp] + delta);
		}
	}

	/** Track button click: plain selects one track (ending the group), Ctrl adds / removes
	a track from the group, Shift adds the tracks from the selected one to it. */
	void clickTrack(int t, bool ctrl, bool shift) {
		if (ctrl) {
			if (t != selectedTrack)
				trackGroup ^= uint16_t(1u << t);
		}
		else if (shift) {
			for (int i = std::min(t, selectedTrack); i <= std::max(t, selectedTrack); i++)
				trackGroup |= uint16_t(1u << i);
		}
		else {
			trackGroup = 0;
			selectTrack(t);
		}
		trackGroup &= uint16_t(~(1u << selectedTrack));
	}

	void selectTrack(int t) {
		lockSel = 0;
		selectedTrack = t;
		selectedCycle = std::min(selectedCycle, cycles().count - 1);
		loadKnobs();
	}

	void selectCycle(int c) {
		selectedCycle = clamp(c, 0, kMaxCycles - 1);
		cycles().count = std::max(cycles().count, selectedCycle + 1); // picking a slot past the end adds cycles
		loadKnobs();
	}

	void requestCycleAction(int action, int argument = 0) {
		cycleAction = action << 8 | argument;
	}

	void applyCycleAction() {
		int a = cycleAction.exchange(0);
		int argument = a & 0xff;
		switch (a >> 8) {
			case CYCLE_SELECT: selectCycle(argument); return;
			case CYCLE_COUNT:
				cycles().count = argument;
				selectedCycle = std::min(selectedCycle, argument - 1);
				break;
			case CYCLE_CLEAR_ALL: cycles().cycles[selectedCycle].clearChanges(); break;
			case CYCLE_CLEAR_STEPS: {
				CycleOverrides& o = cycles().cycles[selectedCycle];
				o.ownHits = false;
				o.manualHits = o.removedHits = 0;
				break;
			}
			case CYCLE_CLEAR_PARAM: cycles().clear(selectedCycle, TrackParam(argument)); break;
			case CYCLE_CLEAR_RANDOM: cycles().clearRandom(selectedCycle, TrackParam(argument)); break;
			case CYCLE_CLEAR_LOCKS: cycles().cycles[selectedCycle].locks.reset(); break;
			case CYCLE_CLEAR_TONAL: cycles().cycles[selectedCycle].ownTonal = false; break;
			default: return;
		}
		loadKnobs();
	}

	int pageCount() {
		return (viewTrack().steps() + 15) / 16;
	}

	/** Lowest note of the KEYS page keyboard. */
	int keysBase() {
		return 12 + 12 * keysOctave;
	}

	/** A KEYS page key click. With steps selected for locking it edits their pool lock;
	otherwise the pool (the edited cycle's in Cycle edit). Out-of-scale keys can't be added,
	but a pool note left outside by a scale change can still be removed. */
	void pressKey(int note) {
		if (lockSel) {
			toggleStepPoolNote(note);
			return;
		}
		Track view = viewTrack();
		if (view.inPool(note) || inScale(note, view.scaleMask(), view.root))
			tonalTarget().togglePoolNote(note);
	}

	int ppqnIn() {
		return kPpqnChoices[ppqnInIndex];
	}

	int ppqnOut() {
		return kPpqnChoices[ppqnOutIndex];
	}

	/** Knobs jump to the selected track's stored values (or amounts). Amounts don't snap,
	so list knobs move smoothly under Rnd assign. */
	void loadKnobs() {
		for (int tp = 0; tp < kNumTrackParams; tp++) {
			paramQuantities[knobOf(tp)]->snapEnabled = kTrackParams[tp].snap && !assigning(tp);
			params[knobOf(tp)].setValue(knobValue(tp));
		}
		params[VARIANCE_PARAM].setValue(random().variance);
		params[RANDOM_RATE_PARAM].setValue(float(random().rate));
	}

	/** Back to the start: the next window begins just before time 0 so step 1 plays. */
	void rewind() {
		beat = 0.0;
		lastBeat = -1e-9;
		extPulses = -1;
		clockOutCount = -1;
		eopCount = 0;
		seq.rewind();
	}

	void applyEvents() {
		bool turnedOff[kNumTracks] = {};
		for (const NoteEvent& e : events) {
			int t = e.track;
			if (e.on) {
				if (gate[t] || turnedOff[t])
					retrig[t] = 1e-3f;
				gate[t] = true;
				velocityV[t] = e.velocity * 10.f / 127.f;
				CcLane& lane = ccLane[t];
				lane.on = e.cc;
				if (e.cc) {
					lane.number = e.ccNumber;
					lane.from = lane.value;
					lane.to = e.velocity;
					lane.start = e.time;
					lane.glide = e.glide;
					chordN[t] = 1;
					midi.off(t); // a note from before the type change
					continue;
				}
				pitchV[t] = (e.note - kBaseNote) / 12.f;
				chordN[t] = clamp(e.chordSize, 1, kMaxPool);
				for (int i = 0; i < chordN[t]; i++)
					chordV[t][i] = (e.chord[i] - kBaseNote) / 12.f;
				midi.noteOn(t, e.chord, chordN[t], e.velocity);
			}
			else {
				gate[t] = false;
				turnedOff[t] = true;
				midi.off(t);
			}
		}
		events.clear();
	}

	void allNotesOff() {
		events.clear();
		seq.allNotesOff(beat, events);
		applyEvents();
	}

	void setPlaying(bool p) {
		if (p == playing)
			return;
		playing = p;
		songEndHold = false;
		// Song on but not started (just loaded, or turned on): it starts with Play.
		if (playing && seq.songOn() && seq.songEntry() < 0 && !seq.songPending() && !seq.songOverridden()) {
			seq.startSong(lastBeat, events);
			applyEvents();
		}
		if (playing)
			midi.start(beat);
		else {
			allNotesOff();
			midi.stop();
		}
	}

	void doReset() {
		allNotesOff();
		rewind();
		// With the song on, Reset puts it at its first entry.
		if (seq.songOn()) {
			seq.startSong(lastBeat, events);
			applyEvents();
		}
		resetOutPulse.trigger(1e-3f);
		if (playing)
			midi.start(0.0); // external gear restarts with us
	}

	void onRemove() override {
		midi.allOff();
	}

	void tap(int64_t frame, float sampleTime) {
		if (lastTapFrame >= 0) {
			float interval = (frame - lastTapFrame) * sampleTime;
			if (interval > 0.2f && interval < 2.5f) {
				for (int i = 3; i > 0; i--)
					tapIntervals[i] = tapIntervals[i - 1];
				tapIntervals[0] = interval;
				tapCount = std::min(tapCount + 1, 4);
				float sum = 0.f;
				for (int i = 0; i < tapCount; i++)
					sum += tapIntervals[i];
				params[TEMPO_PARAM].setValue(clamp(std::round(60.f * tapCount / sum), 24.f, 280.f));
			}
			else {
				tapCount = 0;
			}
		}
		lastTapFrame = frame;
	}

	// ------------------------------------------------------------ process

	void process(const ProcessArgs& args) override {
		applyCycleAction();
		applyPatternAction();
		applySongAction();
		applyLaneEdits();
		// Knobs edit the selected track.
		for (int tp = 0; tp < kNumTrackParams; tp++) {
			// Only a moved knob writes: in Cycle edit it overrides that parameter in the
			// selected cycle; otherwise it moves the track and every cycle's override with it.
			float v = params[knobOf(tp)].getValue();
			if (v == knobValue(tp))
				continue;
			if (knobLocked(knobOf(tp)))
				params[knobOf(tp)].setValue(knobValue(tp)); // e.g. a typed value: put it back
			else if (assigning(tp))
				setRandomAmount(tp, clamp(knobToAmount(tp, v), -100.f, 100.f));
			else if (lockEditing(tp)) {
				for (int pos = 0; pos < kMaxSteps; pos++) {
					if (hasHit(lockSel, pos))
						setLock(pos, tp, v);
				}
			}
			else if (cycleEdit)
				cycles().set(selectedCycle, TrackParam(tp), v);
			else
				setGroupBase(tp, v);
		}
		// Variance and Rate belong to the selected track.
		random().variance = params[VARIANCE_PARAM].getValue();
		random().rate = int(params[RANDOM_RATE_PARAM].getValue());
		for (int t = 0; t < kNumTracks; t++)
			seq.soloCycle[t] = cycleEdit && t == selectedTrack ? selectedCycle : -1;

		// Harmony is endless: each detent moves one pool note a scale step.
		int detents = int(std::lround(params[HARMONY_PARAM].getValue() - harmonyLast));
		uint16_t group = groupMask();
		for (int t = 0; t < kNumTracks && detents != 0; t++) {
			if (group & (1u << t)) {
				Track& target = t == selectedTrack ? tonalTarget() : seq.pattern.tracks[t];
				harmonyUndo.before(t, harmonySlot(t), target);
				for (int i = 0; i < std::abs(detents); i++)
					target.harmony(detents);
				harmonyUndo.after(t, target);
			}
		}
		harmonyLast += float(detents);

		// Knob menu: back to the pool the current run of detents started from. Ask has()
		// first, so a group member with nothing to restore doesn't give its cycle a pool.
		if (harmonyRestoreRequest.exchange(false)) {
			for (int t = 0; t < kNumTracks; t++) {
				if (!(group & (1u << t)) || !harmonyUndo.has(t, harmonySlot(t)))
					continue;
				Track& target = t == selectedTrack ? tonalTarget() : seq.pattern.tracks[t];
				harmonyUndo.restore(t, harmonySlot(t), target);
			}
		}

		if (randomLockRequest.exchange(false)) {
			if (random().locked)
				random().locked = false;
			else
				seq.lockRandom(selectedTrack, lastBeat);
		}

		// IN expander: Transpose, Phrase CV and parameter CV per track (+-5 V = the full range).
		const InMessage* in = inExpander();
		for (int t = 0; t < kNumTracks; t++) {
			seq.transpose[t] = in ? int(std::lround(in->transpose[t] * 12.f)) : 0;
			seq.phraseCv[t] = in ? in->phrase[t] : 0.f;
			for (int k = 0; k < kNumModParams; k++) {
				const ParamInfo& info = kTrackParams[kModParams[k]];
				seq.mod[t][kModParams[k]] = in ? in->cv[k][t] / 5.f * (info.max - info.min) : 0.f;
			}
		}

		uiClock += args.sampleTime;
		handleButtons(args);
		advanceClock(args);
		if (!playing) {
			// Stopped: "Immediately" mutes and Ctrl+click cuts still take effect.
			seq.applyDueMutes(lastBeat, events);
			applyEvents();
		}

		if (playing) {
			seq.process(lastBeat, beat, events);
			lastBeat = beat;
			applyEvents();
			if (seq.takeSongEnd()) {
				// The song ended (no Loop): stop, and back to its start for the next Play.
				setPlaying(false);
				doReset();
				songEndHold = true;
			}
		}
		// A switch (queued, chained, or at once from a click) reloads the controls and counts
		// as a pattern end.
		if (seq.patternSwitches() != lastSwitches) {
			lastSwitches = seq.patternSwitches();
			patternChanged();
			eopCount = 0;
			eopPulse.trigger(1e-3f);
		}
		// The song restarting its pattern within an entry: a pattern end too.
		if (seq.patternRestarts() != lastRestarts) {
			lastRestarts = seq.patternRestarts();
			eopCount = 0;
			eopPulse.trigger(1e-3f);
		}
		if (playing) {

			long c = long(std::floor(beat * ppqnOut()));
			if (c != clockOutCount) {
				clockOutCount = c;
				clockOutPulse.trigger(1e-3f);
			}
			double pq = seq.patternQuarters();
			if (pq > 0) {
				long n = long(std::floor(std::max(beat - seq.patternOrigin(), 0.0) / pq));
				if (n > eopCount)
					eopPulse.trigger(1e-3f);
				eopCount = n;
			}
		}
		if (playing)
			midi.tick(beat);
		if (killRequested.exchange(false)) {
			midi.panic();
			allNotesOff();
		}

		ccDivider.setDivision(std::max(int(args.sampleRate / 1000.f), 1));
		bool sendCc = ccDivider.process();
		for (int t = 0; t < kNumTracks; t++) {
			CcLane& lane = ccLane[t];
			if (lane.on) {
				float f = lane.glide > 0.0 ? clamp(float((beat - lane.start) / lane.glide), 0.f, 1.f) : 1.f;
				lane.value = lane.from + (lane.to - lane.from) * f;
				pitchV[t] = chordV[t][0] = lane.value * 10.f / 127.f;
				if (sendCc)
					midi.cc(t, lane.number, int(std::lround(lane.value)));
			}
			if (retrig[t] > 0.f)
				retrig[t] -= args.sampleTime;
			outputs[PITCH_OUTPUT].setVoltage(pitchV[t], t);
			outputs[GATE_OUTPUT].setVoltage(gate[t] && retrig[t] <= 0.f ? 10.f : 0.f, t);
			outputs[VELOCITY_OUTPUT].setVoltage(velocityV[t], t);
		}
		sendToOutExpander();
		outputs[PITCH_OUTPUT].setChannels(kNumTracks);
		outputs[GATE_OUTPUT].setChannels(kNumTracks);
		outputs[VELOCITY_OUTPUT].setChannels(kNumTracks);
		outputs[CLOCK_OUTPUT].setVoltage(clockOutPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[RESET_OUTPUT].setVoltage(resetOutPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[EOP_OUTPUT].setVoltage(eopPulse.process(args.sampleTime) ? 10.f : 0.f);

		if (lightDivider.process())
			updateLights();
	}

	/** The IN expander's latest inputs if one sits directly to the left. */
	const InMessage* inExpander() {
		Module* in = leftExpander.module;
		return in && in->model == modelTaleaIn ? static_cast<const InMessage*>(leftExpander.consumerMessage) : nullptr;
	}

	/** Feeds an Talea OUT placed directly to the right (one sample later, via Rack's
	double-buffered expander messages). */
	void sendToOutExpander() {
		Module* out = rightExpander.module;
		if (!out || out->model != modelTaleaOut)
			return;
		OutMessage* m = static_cast<OutMessage*>(out->leftExpander.producerMessage);
		if (!m)
			return;
		for (int t = 0; t < kNumTracks; t++) {
			m->channels[t] = chordN[t];
			std::copy(chordV[t], chordV[t] + kMaxPool, m->pitch[t]);
			m->gate[t] = gate[t] && retrig[t] <= 0.f;
			m->velocity[t] = velocityV[t];
			m->muted[t] = seq.pattern.tracks[t].muted;
		}
		out->leftExpander.requestMessageFlip();
	}

	void handleButtons(const ProcessArgs& args) {
		auto pressed = [&](dsp::BooleanTrigger& t, int paramId) {
			return t.process(params[paramId].getValue() > 0.f);
		};
		if (pressed(playTrigger, PLAY_PARAM))
			setPlaying(!playing);
		if (inputs[RUN_INPUT].isConnected()) {
			bool rising = runInTrigger.process(inputs[RUN_INPUT].getVoltage(), 0.1f, 1.f);
			if (runGateMode) {
				// After the song ended, the gate has to go low before it runs again.
				if (!runInTrigger.isHigh())
					songEndHold = false;
				if (!songEndHold)
					setPlaying(runInTrigger.isHigh());
			}
			else if (rising)
				setPlaying(!playing);
		}
		bool resetIn = resetInTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
		if (pressed(resetTrigger, RESET_PARAM) || resetIn)
			doReset();
		if (pressed(tapTrigger, TAP_PARAM))
			tap(args.frame, args.sampleTime);

		// Temp: click toggles it (turning it off reverts); Ctrl+click keeps the changes.
		if (pressed(tempTrigger, TEMP_PARAM)) {
			if (tempCtrl.exchange(false) && seq.tempActive())
				endTemp(true);
			else
				tempLatched ^= true;
		}
		switch (tempRequest.exchange(TEMP_REQUEST_NONE)) {
			case TEMP_REQUEST_KEEP:
				if (seq.tempActive())
					endTemp(true);
				break;
			case TEMP_REQUEST_DISCARD:
				if (seq.tempActive())
					endTemp(false);
				break;
			default: break;
		}
		const InMessage* in = inExpander();
		tempInTrigger.process(in ? in->temp : 0.f, 0.1f, 1.f);
		bool temp = tempLatched || tempInTrigger.isHigh();
		if (temp && !seq.tempActive())
			seq.beginTemp();
		else if (!temp && seq.tempActive())
			endTemp(false);

		if (pressed(allTrigger, ALL_PARAM))
			allTracks ^= true;
		// Clicking the armed button again disarms it; PASTE arms only with something to paste.
		if (pressed(copyTrigger, COPY_PARAM))
			armed = armed == ARM_COPY ? ARM_NONE : ARM_COPY;
		if (pressed(pasteTrigger, PASTE_PARAM))
			armed = armed == ARM_PASTE || clip.kind == CLIP_NONE ? ARM_NONE : ARM_PASTE;
		// CLEAR: Ctrl+click arms it to stay armed (also when it is armed already); a plain
		// click on an armed CLEAR disarms it.
		if (pressed(clearTrigger, CLEAR_PARAM)) {
			bool sticky = clearCtrl.exchange(false);
			if (armed == ARM_CLEAR && !sticky)
				armed = ARM_NONE;
			else {
				armed = ARM_CLEAR;
				clearSticky = sticky;
			}
		}
		int knob = knobClick.exchange(-1);
		if (knob >= 0)
			clipKnob(knob);
		if (pressed(rndTrigger, RND_ASSIGN_PARAM)) {
			rndAssign ^= true;
			loadKnobs();
		}
		if (pressed(cycleEditTrigger, CYCLE_EDIT_PARAM)) {
			cycleEdit ^= true;
			loadKnobs();
		}
		if (pressed(followTrigger, FOLLOW_PARAM))
			follow ^= true;
		for (int i = MODE_STEP; i < kNumModes; i++) {
			if (!pressed(modeTriggers[i], MODE_TRACK_PARAM + i))
				continue;
			if (i == MODE_KEYS) // retired: the KEYS page replaced it (no button on the panel)
				continue;
			// Leaving MUTE mode drops staged mutes, leaving STEP mode the lock selection.
			// MUTE again applies the staged mutes; STEP again ends the lock selection.
			if (i == MODE_MUTE && padMode == MODE_MUTE)
				seq.commitMutes(lastBeat);
			else if (i == MODE_STEP && padMode == MODE_STEP)
				clearLockSelection();
			else {
				if (padMode == MODE_MUTE)
					seq.clearStagedMutes();
				if (i == MODE_MUTE)
					preMuteMode = padMode;
				clearLockSelection();
				padMode = i;
			}
		}
		// MUTE mode is a view like the others: a page tab leaves it (dropping staged mutes),
		// back to the pad mode before it.
		int key = keyRequest.exchange(-1);
		if (key >= 0)
			pressKey(key);
		else if (key == kKeyTrackPool)
			clearStepPools();
		if (leaveMuteRequest.exchange(false) && padMode == MODE_MUTE) {
			seq.clearStagedMutes();
			padMode = preMuteMode;
		}
		if (muteApplyTrigger.process(in ? in->muteApply : 0.f, 0.1f, 1.f))
			seq.commitMutes(lastBeat);
		// Mute gates: a patched track follows its gate (high = muted) at each change; the pads
		// still work in between. Regenerate triggers give a track's random a new seed.
		for (int t = 0; t < kNumTracks; t++) {
			if (in && in->muteOn[t]) {
				muteGate[t].process(in->mute[t], 0.1f, 1.f);
				if (muteGate[t].isHigh() != muteGateHigh[t]) {
					muteGateHigh[t] = muteGate[t].isHigh();
					seq.setMuted(t, muteGateHigh[t], lastBeat, events);
				}
			}
			else
				muteGateHigh[t] = false;
			if (regenTrigger[t].process(in ? in->regen[t] : 0.f, 0.1f, 1.f))
				seq.pattern.random[t].regenerate();
		}
		switch (muteRequest.exchange(MUTE_REQUEST_NONE)) {
			case MUTE_REQUEST_APPLY: seq.commitMutes(lastBeat); break;
			case MUTE_REQUEST_CLEAR: seq.clearMuteQueue(); break;
			default: break;
		}
		// Pattern input: 0-10 V in 16 bands; a new band selects like a click.
		if (in && in->patternOn) {
			int slot = clamp(int(in->pattern * kNumPatterns / 10.f), 0, kNumPatterns - 1);
			if (slot != lastPatternIn)
				selectPattern(slot, false, false);
			lastPatternIn = slot;
		}
		else
			lastPatternIn = -1;
		for (int i = 0; i < kNumStepPages; i++) {
			if (!pressed(pageTriggers[i], STEP_PAGE_PARAM + i))
				continue;
			if (i < pageCount()) {
				stepPage = i;
				follow = false;
			}
		}
		stepPage = std::min(stepPage, pageCount() - 1);

		for (int i = 0; i < kNumTracks; i++) {
			if (!pressed(trackTriggers[i], TRACK_SELECT_PARAM + i))
				continue;
			bool ctrl = trackCtrl[i].exchange(false), shift = trackShift[i].exchange(false);
			if (!clipTrack(i))
				clickTrack(i, ctrl, shift);
		}
		for (int i = 0; i < kNumPads; i++) {
			if (!pressed(padTriggers[i], PAD_PARAM + i))
				continue;
			bool ctrl = padCtrl[i].exchange(false);
			bool shift = padShift[i].exchange(false);
			if (clipPad(i))
				continue;
			if (padMode == MODE_PATTERN && songPageShown) {
				// SONG page: a pattern pad adds the pattern to the song.
				if (seq.song.count < talea::kMaxSongEntries) {
					seq.songInsert(seq.song.count, {i, seq.defaultBars(i)}, lastBeat);
					songSel = seq.song.count - 1;
				}
			}
			else if (padMode == MODE_PATTERN)
				selectPattern(i, shift, ctrl);
			else if (padMode == MODE_MUTE) {
				// Click stages a change; Ctrl+click mutes or unmutes at once.
				if (ctrl)
					seq.setMuted(i, !seq.pattern.tracks[i].muted, lastBeat, events);
				else
					seq.stageMute(i);
			}
			else if (padMode == MODE_STEP) {
				// Ctrl+click selects steps for locking. In Cycle edit a step toggle belongs to
				// the edited cycle; otherwise to the track (cycles with step edits of their own
				// keep them).
				Track view = viewTrack();
				int step = stepPage * 16 + i;
				if (ctrl) {
					if (step < view.steps()) {
						lockSel ^= StepMask(1) << view.unrotated(step);
						loadKnobs();
					}
				}
				else if (cycleEdit)
					cycles().toggleStep(track(), selectedCycle, stepPage * 16 + i);
				else
					track().toggleStep(stepPage * 16 + i);
			}
			else if (padMode == MODE_CYCLE) {
				// Click selects a cycle (past the last adds cycles); Ctrl+click mutes it. Pads
				// past kMaxCycles do nothing.
				if (i >= kMaxCycles)
					continue;
				if (ctrl)
					cycles().cycles[i].muted ^= true;
				else
					selectCycle(i);
			}
		}
	}

	void advanceClock(const ProcessArgs& args) {
		samplesSincePulse++;
		bool pulse = clockInTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
		if (pulse) {
			if (samplesSincePulse < int64_t(args.sampleRate * 4))
				periodSamples = double(samplesSincePulse);
			samplesSincePulse = 0;
		}
		if (!inputs[CLOCK_INPUT].isConnected()) {
			displayBpm = params[TEMPO_PARAM].getValue();
			if (playing)
				beat += args.sampleTime * displayBpm / 60.0;
			return;
		}
		if (periodSamples > 0)
			displayBpm = float(60.0 / (periodSamples * args.sampleTime * ppqnIn()));
		if (!playing)
			return;
		if (pulse) {
			extPulses++;
			extPulseBeat = double(extPulses) / ppqnIn();
			beat = std::max(beat, extPulseBeat);
		}
		else if (extPulses >= 0 && periodSamples > 0) {
			// Interpolate between pulses from the measured period, never past the next one.
			double frac = std::min(samplesSincePulse / periodSamples, 0.999);
			beat = std::max(beat, extPulseBeat + frac / ppqnIn());
		}
	}

	void updateLights() {
		Track live = seq.settingsAt(selectedTrack, lastBeat);
		std::copy(live.values, live.values + kNumTrackParams, liveValue);
		for (int tp = 0; tp < kNumTrackParams; tp++)
			liveShown[tp] = knobValue(tp);
		lights[PLAY_LIGHT].setBrightness(playing);
		bool clockStarted = !inputs[CLOCK_INPUT].isConnected() || extPulses >= 0;
		lights[CLOCK_LIGHT].setBrightness(playing && clockStarted && beat - std::floor(beat) < 0.1);
		// Temp: solid while the gate holds it, blinking while latched.
		bool slowBlink = std::fmod(uiClock, 0.5) < 0.25;
		lights[TEMP_LIGHT].setBrightness(tempInTrigger.isHigh() || (tempLatched && slowBlink));
		lights[ALL_LIGHT].setBrightness(allTracks);
		lights[RND_ASSIGN_LIGHT].setBrightness(rndAssign);
		lights[CYCLE_EDIT_LIGHT].setBrightness(cycleEdit);
		lights[FOLLOW_LIGHT].setBrightness(follow);
		bool fastBlink = std::fmod(uiClock, 0.2) < 0.1;
		for (int i = 0; i < kNumModes; i++)
			lights[MODE_LIGHT + i].setBrightness(padMode == i);
		if (seq.scheduledMutes())
			lights[MODE_LIGHT + MODE_MUTE].setBrightness(fastBlink); // committed, waiting to land
		if (seq.queuedPattern() >= 0 && seq.chainLength() < 2)
			lights[MODE_LIGHT + MODE_PATTERN].setBrightness(fastBlink); // a switch is waiting
		// COPY / CLEAR blink while armed; PASTE shows the clipboard (green = track, cycle or
		// parameter, blue = pattern), dim while it holds something, blinking while armed.
		float armedBlink = fastBlink ? 1.f : 0.f;
		lights[COPY_LIGHT + 0].setBrightness(armed == ARM_COPY ? armedBlink : 0.f);
		lights[COPY_LIGHT + 1].setBrightness(armed == ARM_COPY ? armedBlink : 0.f);
		float paste = clip.kind == CLIP_NONE ? 0.f : armed == ARM_PASTE ? armedBlink : 0.25f;
		lights[PASTE_LIGHT + 0].setBrightness(clip.kind == CLIP_PATTERN ? 0.f : paste);
		lights[PASTE_LIGHT + 1].setBrightness(clip.kind == CLIP_PATTERN ? paste : 0.f);
		lights[CLEAR_LIGHT + 0].setBrightness(armed == ARM_CLEAR ? armedBlink : 0.f);
		lights[CLEAR_LIGHT + 1].setBrightness(armed == ARM_CLEAR ? armedBlink : 0.f);

		int playStep = playing || beat > 0 ? seq.stepAt(selectedTrack, beat) : -1;
		int playPage = playStep >= 0 ? playStep / 16 : -1;
		if (follow && playPage >= 0 && padMode == MODE_STEP)
			stepPage = playPage;
		bool blink = std::fmod(beat, 1.0) < 0.5;
		for (int i = 0; i < kNumStepPages; i++) {
			float b = i == stepPage ? 1.f : i < pageCount() ? 0.15f : 0.f;
			if (i == playPage && i != stepPage && blink)
				b = 0.6f;
			lights[STEP_PAGE_LIGHT + i].setBrightness(b);
		}

		Track view = viewTrack();
		StepMask hits = seq.hits(selectedTrack, cycleEdit ? selectedCycle : -1);
		int playCycleNow = seq.cycleAt(selectedTrack, beat);
		// Outside Cycle edit the pads show the track (what clicks edit) and mark how the
		// playing cycle differs: dim red = it adds the step, dark orange = it drops it.
		StepMask playHits = !cycleEdit && cycles().count > 1 ? seq.hits(selectedTrack, playCycleNow) : hits;
		int n = view.steps();
		int played = view.playedSteps();
		int playCycle = playCycleNow;
		bool started = playing || beat > 0;
		for (int i = 0; i < kNumPads; i++) {
			float r = 0.f, g = 0.f, b = 0.f;
			if (padMode == MODE_MUTE) {
				// Playing = orange (white on a note), muted = blue-grey. A staged change
				// blinks slowly to the state it will become, a committed one quickly.
				bool muted = seq.pattern.tracks[i].muted;
				uint16_t bit = uint16_t(1u << i);
				if (seq.scheduledMutes() & bit)
					muted ^= fastBlink;
				else if (seq.stagedMutes() & bit)
					muted ^= slowBlink;
				if (muted) {
					r = 0.12f;
					g = 0.17f;
					b = 0.28f;
				}
				else if (gate[i])
					r = g = b = 1.f;
				else {
					r = 0.94f;
					g = 0.54f;
					b = 0.14f;
				}
			}
			else if (padMode == MODE_STEP) {
				int step = stepPage * 16 + i;
				if (step < n) {
					bool hit = hasHit(hits, step);
					if (step >= played) {
						// Cut off by Length: never plays.
						r = hit ? 0.24f : 0.02f;
						g = hit ? 0.14f : 0.02f;
						b = hit ? 0.04f : 0.02f;
					}
					else if (step == playStep)
						r = g = b = hasHit(playHits, step) ? 1.f : 0.35f;
					else if (hasHit(lockSel, view.unrotated(step))) {
						// Selected for locking: blue.
						r = hit ? 0.3f : 0.08f;
						g = hit ? 0.6f : 0.2f;
						b = hit ? 1.f : 0.5f;
					}
					else if (locksAt(view.unrotated(step))) {
						// Has locks: light blue.
						r = hit ? 0.45f : 0.04f;
						g = hit ? 0.8f : 0.14f;
						b = hit ? 0.95f : 0.18f;
					}
					else if (hit && !hasHit(playHits, step)) {
						r = 0.32f;
						g = 0.15f;
						b = 0.03f;
					}
					else if (hit) {
						r = 0.94f;
						g = 0.54f;
						b = 0.14f;
					}
					else if (hasHit(playHits, step)) {
						r = 0.5f;
						g = 0.06f;
						b = 0.04f;
					}
					else {
						r = g = 0.06f;
						b = 0.07f;
					}
				}
			}
			else if (padMode == MODE_CYCLE) {
				// Slots in use: orange = selected (red while editing it), blue-grey = muted,
				// dim red = has overrides, dim = plain. The playing cycle flashes white on notes.
				const TrackCycles& cyc = cycles();
				if (i < cyc.count) {
					bool sel = i == selectedCycle;
					if (sel && cycleEdit) {
						r = 1.f;
						g = 0.12f;
						b = 0.08f;
					}
					else if (sel) {
						r = 0.94f;
						g = 0.54f;
						b = 0.14f;
					}
					else if (cyc.cycles[i].muted) {
						r = 0.12f;
						g = 0.17f;
						b = 0.28f;
					}
					else if (cyc.cycles[i].modified()) {
						r = 0.35f;
						g = 0.07f;
						b = 0.05f;
					}
					else
						r = g = b = 0.12f;
					if (started && i == playCycle) {
						if (gate[selectedTrack])
							r = g = b = 1.f;
						else if (!sel)
							r = g = b = std::max(r, 0.4f);
					}
				}
			}
			else if (padMode == MODE_PATTERN) {
				// Orange = playing, blinking = queued, dim = holds a pattern, dark = default.
				// Chain entries not playing are dim red.
				bool inChain = false;
				for (int c = 0; c < seq.chainLength(); c++)
					inChain |= seq.chainEntry(c) == i;
				if (i == seq.currentPattern() || (i == seq.queuedPattern() && fastBlink)) {
					r = 0.94f;
					g = 0.54f;
					b = 0.14f;
				}
				else if (inChain) {
					r = 0.5f;
					g = 0.06f;
					b = 0.04f;
				}
				else if (seq.patternUsed(i)) {
					r = 0.24f;
					g = 0.14f;
					b = 0.04f;
				}
				else
					r = g = b = 0.06f;
			}
			setRgb(PAD_LIGHT + 3 * i, r, g, b);
		}
		// Track-select row: white on notes, red = selected, dim red = grouped, blue-grey = muted,
		// dim orange = holds data, dark = default.
		uint16_t group = groupMask();
		for (int i = 0; i < kNumTracks; i++) {
			if (gate[i])
				setRgb(TRACK_SELECT_LIGHT + 3 * i, 1.f, 1.f, 1.f);
			else if (i == selectedTrack)
				setRgb(TRACK_SELECT_LIGHT + 3 * i, 1.f, 0.12f, 0.08f);
			else if (group & (1u << i))
				setRgb(TRACK_SELECT_LIGHT + 3 * i, 0.4f, 0.05f, 0.03f);
			else if (seq.pattern.tracks[i].muted)
				setRgb(TRACK_SELECT_LIGHT + 3 * i, 0.08f, 0.11f, 0.18f);
			else if (seq.pattern.trackUsed(i))
				setRgb(TRACK_SELECT_LIGHT + 3 * i, 0.24f, 0.14f, 0.04f);
			else
				setRgb(TRACK_SELECT_LIGHT + 3 * i, 0.05f, 0.05f, 0.06f);
		}
	}

	void setRgb(int light, float r, float g, float b) {
		lights[light + 0].setBrightness(r);
		lights[light + 1].setBrightness(g);
		lights[light + 2].setBrightness(b);
	}

	// ------------------------------------------------------------ state

	void onReset() override {
		seq.resetBank();
		seq.patternQuantize = MUTE_PATTERN;
		tempLatched = false;
		armed = ARM_NONE;
		clip.kind = CLIP_NONE;
		// MIDI: notes off, channels back to track n -> channel n, clock off (device kept).
		midi.allOff();
		for (int t = 0; t < kNumTracks; t++)
			midi.channel[t] = t;
		midi.clock = false;
		lockSel = 0;
		trackGroup = 0;
		selectedTrack = 0;
		padMode = MODE_STEP;
		stepPage = 0;
		follow = true;
		allTracks = rndAssign = cycleEdit = false;
		selectedCycle = 0;
		keysOctave = 2;
		seq.songClear();
		seq.restoreSongOn(false);
		songSel = songPage = 0;
		songFollow = true;
		harmonyLast = params[HARMONY_PARAM].getValue();
		seq.clearMuteQueue();
		seq.muteTiming = MUTE_NOW;
		seq.muteCuts = false;
		playing = false;
		allNotesOff();
		rewind();
		loadKnobs();
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		// The bank: slots holding something other than the default pattern. Temp edits aren't
		// saved unless kept.
		json_t* patterns = json_array();
		for (int i = 0; i < kNumPatterns; i++) {
			if (!seq.patternUsed(i))
				continue;
			json_t* slot = json_object();
			json_object_set_new(slot, "slot", json_integer(i));
			json_object_set_new(slot, "core", json_string(patternToJson(seq.patternSlot(i)).c_str()));
			json_array_append_new(patterns, slot);
		}
		json_object_set_new(root, "patterns", patterns);
		json_object_set_new(root, "currentPattern", json_integer(seq.currentPattern()));
		json_object_set_new(root, "patternQuantize", json_integer(seq.patternQuantize));
		json_object_set_new(root, "selectedTrack", json_integer(selectedTrack));
		json_object_set_new(root, "trackGroup", json_integer(trackGroup));
		json_object_set_new(root, "allTracks", json_boolean(allTracks));
		json_object_set_new(root, "selectedCycle", json_integer(selectedCycle));
		json_object_set_new(root, "padMode", json_integer(padMode));
		json_object_set_new(root, "follow", json_boolean(follow));
		json_object_set_new(root, "displayFollows", json_boolean(displayFollows));
		json_object_set_new(root, "keysOctave", json_integer(keysOctave));
		json_object_set_new(root, "ppqnIn", json_integer(ppqnIn()));
		json_object_set_new(root, "ppqnOut", json_integer(ppqnOut()));
		json_object_set_new(root, "runGateMode", json_boolean(runGateMode));
		json_object_set_new(root, "muteTiming", json_integer(seq.muteTiming));
		json_object_set_new(root, "muteCuts", json_boolean(seq.muteCuts));
		json_object_set_new(root, "midi", midi.toJson());
		json_object_set_new(root, "song", json_string(songToJson(seq.song).c_str()));
		json_object_set_new(root, "songOn", json_boolean(seq.songOn()));
		json_object_set_new(root, "songFollow", json_boolean(songFollow));
		return root;
	}

	static int ppqnIndexOf(json_t* j, int fallback) {
		if (!j)
			return fallback;
		for (int i = 0; i < kNumPpqnChoices; i++) {
			if (kPpqnChoices[i] == json_integer_value(j))
				return i;
		}
		return fallback;
	}

	void dataFromJson(json_t* root) override {
		seq.resetBank();
		Pattern p;
		auto load = [&](json_t* core, int slot) {
			if (core && json_string_value(core) && patternFromJson(json_string_value(core), p))
				seq.storePattern(slot, p);
		};
		if (json_t* patterns = json_object_get(root, "patterns")) {
			size_t i;
			json_t* slot;
			json_array_foreach(patterns, i, slot) {
				int n = int(json_integer_value(json_object_get(slot, "slot")));
				if (n >= 0 && n < kNumPatterns)
					load(json_object_get(slot, "core"), n);
			}
		}
		else
			load(json_object_get(root, "core"), 0); // before the bank: one pattern
		if (json_t* j = json_object_get(root, "currentPattern"))
			seq.setCurrentPattern(clamp(int(json_integer_value(j)), 0, kNumPatterns - 1));
		if (json_t* j = json_object_get(root, "patternQuantize"))
			seq.patternQuantize = clamp(int(json_integer_value(j)), 0, kNumMuteTimings - 1);
		lastSwitches = seq.patternSwitches();
		tempLatched = false;
		if (json_t* j = json_object_get(root, "selectedTrack"))
			selectedTrack = clamp(int(json_integer_value(j)), 0, kNumTracks - 1);
		if (json_t* j = json_object_get(root, "trackGroup"))
			trackGroup = uint16_t(json_integer_value(j)) & uint16_t(~(1u << selectedTrack));
		if (json_t* j = json_object_get(root, "allTracks"))
			allTracks = json_boolean_value(j);
		if (json_t* j = json_object_get(root, "selectedCycle"))
			selectedCycle = clamp(int(json_integer_value(j)), 0, cycles().count - 1);
		if (json_t* j = json_object_get(root, "padMode"))
			padMode = clamp(int(json_integer_value(j)), int(MODE_STEP), kNumModes - 1);
		if (padMode == MODE_KEYS) // KEYS was a pad mode before the KEYS page
			padMode = MODE_STEP;
		if (json_t* j = json_object_get(root, "displayFollows"))
			displayFollows = json_boolean_value(j);
		if (json_t* j = json_object_get(root, "follow"))
			follow = json_boolean_value(j);
		ppqnInIndex = ppqnIndexOf(json_object_get(root, "ppqnIn"), ppqnInIndex);
		ppqnOutIndex = ppqnIndexOf(json_object_get(root, "ppqnOut"), ppqnOutIndex);
		json_t* songJ = json_object_get(root, "song");
		if (!(songJ && json_string_value(songJ) && songFromJson(json_string_value(songJ), seq.song)))
			seq.song = talea::Song();
		seq.restoreSongOn(json_is_true(json_object_get(root, "songOn")));
		if (json_t* j = json_object_get(root, "songFollow"))
			songFollow = json_boolean_value(j);
		songSel = clamp(songSel, 0, std::max(seq.song.count - 1, 0));
		if (json_t* j = json_object_get(root, "runGateMode"))
			runGateMode = json_boolean_value(j);
		if (json_t* j = json_object_get(root, "keysOctave"))
			keysOctave = clamp(int(json_integer_value(j)), 0, kKeysOctaveMax);
		if (json_t* j = json_object_get(root, "muteTiming"))
			seq.muteTiming = clamp(int(json_integer_value(j)), 0, kNumMuteTimings - 1);
		if (json_t* j = json_object_get(root, "muteCuts"))
			seq.muteCuts = json_boolean_value(j);
		midi.fromJson(json_object_get(root, "midi"));
		seq.clearMuteQueue();
		harmonyLast = params[HARMONY_PARAM].getValue();
		loadKnobs();
	}
};

/** Context display (D01): page tabs, an info line for the selected track, and per page
either option rows (clickable selections) with a diagram, or the step grid. */
struct ContextDisplay : widget::OpaqueWidget {
	Talea* module = nullptr;
	int page = 0; // UI-only state
	bool lastRndAssign = false;
	bool lastPatternMode = false;

	const NVGcolor accent = nvgRGB(0xf0, 0x8a, 0x24);
	const NVGcolor text = nvgRGB(0xd8, 0xdc, 0xe2);
	const NVGcolor dim = nvgRGB(0x5a, 0x61, 0x6b);
	const NVGcolor labelColor = nvgRGB(0xc8, 0xcd, 0xd4); // display text: labels, hints, unselected options
	const NVGcolor lockBlue = nvgRGB(0x6c, 0xc4, 0xf2);

	static constexpr float kRowsEndFull = 153.f; // mm: whole display width
	static constexpr float kRowsEndSide = 100.f; // mm: leaves room for a diagram on the right
	static constexpr float kDiagramX = 104.f;    // mm

	/** A label and a strip of choices. Rows stack from the top of the content area. */
	struct OptionRow {
		std::string label;
		int count;
		std::function<std::string(int)> name;
		std::function<bool(int)> on;
		std::function<void(int)> select;
		float endMm;
	};

	static OptionRow choice(std::string label, int count, std::function<std::string(int)> name, int* field, float endMm) {
		return {label, count, name, [=](int i) { return *field == i; }, [=](int i) { *field = i; }, endMm};
	}

	static OptionRow toggle(std::string label, const char* off, const char* on, bool* field, float endMm) {
		return {label, 2, [=](int i) { return std::string(i ? on : off); }, [=](int i) { return *field == (i == 1); },
		        [=](int i) { *field = i == 1; }, endMm};
	}

	static std::string noteName(int note) {
		return string::f("%s%d", kNoteNames[((note % 12) + 12) % 12], note / 12 - 1);
	}

	static std::string poolText(const Track& tr) {
		std::string s;
		for (int i = 0; i < tr.poolSize; i++)
			s += (i ? " " : "") + noteName(tr.pool[i]);
		return s;
	}

	/** "1 3 3 2" for the chain, with the playing entry in brackets. */
	std::string chainText() {
		const talea::Engine& seq = module->seq;
		std::string s;
		for (int i = 0; i < seq.chainLength(); i++) {
			std::string n = std::to_string(seq.chainEntry(i) + 1);
			s += (i ? " " : "") + (i == seq.chainPosition() ? "[" + n + "]" : n);
		}
		return s;
	}

	/** In Cycle edit the SCALE page shows and edits the edited cycle's scale (a copy, written
	back to the cycle on a click). */
	Track tonalScratch;

	bool cycleScale() {
		return module && module->cycleEdit && page == kScalePage && !muteView();
	}

	Track& rowsTrack() {
		if (cycleScale()) {
			tonalScratch = module->viewTrack();
			return tonalScratch;
		}
		return module->track();
	}

	/** A pitch page (KEYS, PHRASE, SCALE, STYLE) while the selected track is a CC track. */
	bool pitchUnused() {
		return module && module->track().cc
		       && (page == kKeysPage || page == kPhrasePage || page == kScalePage || page == kStylePage)
		       && !muteView();
	}

	/** KEYS page: four octaves from the module's keysOctave, between the octave arrows. */
	static constexpr float kArrowW = 6.f; // mm
	static constexpr float kKeysTop = 19.f, kKeysHeight = 23.f; // mm: under the two-line header
	Keyboard keysKeyboard() {
		Keyboard k;
		k.rect = math::Rect(mm2px(Vec(3.f + kArrowW + 1.f, kKeysTop)), mm2px(Vec(153.f - 2 * (kArrowW + 1.f), kKeysHeight)));
		k.first = module ? module->keysBase() : 36;
		k.octaves = kKeysOctaves;
		return k;
	}
	/** TRACK POOL button (steps selected): right of the second header line. */
	math::Rect trackPoolButton() {
		return math::Rect(mm2px(Vec(128.f, 12.f)), mm2px(Vec(25.f, 4.2f)));
	}
	bool stepPoolShown() {
		return module && module->lockSel && module->poolLockAt(module->firstLockStep());
	}

	math::Rect octaveArrow(int dir) {
		return math::Rect(mm2px(Vec(dir < 0 ? 3.f : 156.f - 3.f - kArrowW, kKeysTop)), mm2px(Vec(kArrowW, kKeysHeight)));
	}

	/** SCALE page NOTES: one shorter octave under the rows; a key toggles its pitch class. */
	Keyboard scaleKeyboard() {
		Keyboard k;
		k.rect = math::Rect(mm2px(Vec(21.f, 30.f)), mm2px(Vec(56.f, 13.f)));
		k.first = 60;
		k.labels = false;
		return k;
	}

	/** Editing a scale note turns the current scale into the User scale first. */
	static void toggleScaleNote(Track& tr, int pc) {
		if (tr.scale != kUserScale) {
			tr.userScale = tr.scaleMask();
			tr.scale = kUserScale;
		}
		tr.userScale ^= ScaleMask(1u << (((pc - tr.root) % 12 + 12) % 12));
	}

	void drawKeys(NVGcontext* vg, const Track& view) {
		// With steps selected the keys show the first one's pool: blue = its own pool lock,
		// orange = the track's pool it starts from.
		bool steps = module && module->lockSel;
		Track tr = steps ? module->stepPoolTrack() : view;
		bool own = stepPoolShown();
		int sounding = module && module->gate[module->selectedTrack]
		                   ? int(std::lround(module->pitchV[module->selectedTrack] * 12.f)) + kBaseNote
		                   : -1;
		// Second header line: what a click does.
		nvgFontSize(vg, mm2px(2.6f));
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		nvgFillColor(vg, labelColor);
		nvgText(vg, mm2px(3.f), mm2px(14.6f),
		        steps ? (own ? "SELECTED STEPS PLAY THEIR OWN NOTES: CLICK = ADD / REMOVE"
		                     : "SELECTED STEPS PLAY THE TRACK POOL: CLICK = GIVE THEM THEIR OWN NOTES")
		              : "CLICK = ADD / REMOVE A POOL NOTE   CTRL+CLICK STEP PADS, THEN KEYS = THEIR OWN NOTES",
		        nullptr);
		if (own) {
			math::Rect r = trackPoolButton();
			nvgBeginPath(vg);
			nvgRoundedRect(vg, r.pos.x, r.pos.y, r.size.x, r.size.y, mm2px(0.6f));
			nvgFillColor(vg, nvgRGB(0x23, 0x26, 0x2b));
			nvgFill(vg);
			nvgFontSize(vg, mm2px(2.4f));
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgFillColor(vg, text);
			nvgText(vg, r.pos.x + r.size.x / 2, r.pos.y + r.size.y / 2 + 0.5f, "TRACK POOL", nullptr);
		}
		keysKeyboard().draw(vg, [&](int n) {
			KeyLook l;
			bool scale = inScale(n, tr.scaleMask(), tr.root);
			l.enabled = scale || tr.inPool(n);
			if (tr.inPool(n))
				l.fill = own ? lockBlue : accent;
			l.marker = scale && ((n - tr.root) % 12 + 12) % 12 == 0;
			l.dot = n == sounding;
			return l;
		});
		nvgFontSize(vg, mm2px(3.f));
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		int octave = module ? module->keysOctave : 2;
		for (int dir : {-1, 1}) {
			math::Rect r = octaveArrow(dir);
			bool on = dir < 0 ? octave > 0 : octave < kKeysOctaveMax;
			nvgBeginPath(vg);
			nvgRoundedRect(vg, r.pos.x, r.pos.y, r.size.x, r.size.y, mm2px(0.6f));
			nvgFillColor(vg, nvgRGB(0x23, 0x26, 0x2b));
			nvgFill(vg);
			nvgFillColor(vg, on ? text : dim);
			nvgText(vg, r.pos.x + r.size.x / 2, r.pos.y + r.size.y / 2, dir < 0 ? "<" : ">", nullptr);
		}
	}

	void drawScaleKeys(NVGcontext* vg, const Track& tr) {
		Keyboard k = scaleKeyboard();
		nvgFontSize(vg, mm2px(2.4f));
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgFillColor(vg, labelColor);
		nvgText(vg, mm2px(3.f), k.rect.pos.y + mm2px(2.1f), "NOTES", nullptr);
		k.draw(vg, [&](int n) {
			KeyLook l;
			if (inScale(n, tr.scaleMask(), tr.root))
				l.fill = accent;
			l.marker = ((n - tr.root) % 12 + 12) % 12 == 0;
			return l;
		});
	}

	/** MUTE mode takes over the display whatever the page. */
	bool muteView() {
		return module && module->padMode == MODE_MUTE;
	}

	std::vector<OptionRow> rowsFor(Track& tr) {
		std::vector<OptionRow> rows;
		if (muteView()) {
			const char* timings[kNumMuteTimings] = {"NOW", "BEAT", "BAR", "PATTERN"};
			rows.push_back(choice("APPLY", kNumMuteTimings, [=](int i) { return std::string(timings[i]); },
			                      &module->seq.muteTiming, 80.f));
			rows.push_back(toggle("ON MUTE", "FINISH", "CUT", &module->seq.muteCuts, 80.f));
			return rows;
		}
		switch (page) {
			case kCyclePage: {
				if (!module)
					break;
				Talea* m = module;
				TrackCycles& cyc = m->cycles();
				rows.push_back({"COUNT", kMaxCycles, [](int i) { return std::to_string(i + 1); },
				                [&cyc](int i) { return cyc.count == i + 1; },
				                [m](int i) { m->requestCycleAction(Talea::CYCLE_COUNT, i + 1); },
				                kRowsEndFull});
				// Slot names: * = has overrides, (n) = muted.
				rows.push_back({"CYCLE", kMaxCycles,
				                [&cyc](int i) {
					                std::string n = std::to_string(i + 1) + (cyc.cycles[i].modified() ? "*" : "");
					                return cyc.cycles[i].muted ? "(" + n + ")" : n;
				                },
				                [m](int i) { return m->selectedCycle == i; },
				                [m](int i) { m->requestCycleAction(Talea::CYCLE_SELECT, i); }, kRowsEndFull});
				CycleOverrides& sel = cyc.cycles[m->selectedCycle];
				rows.push_back(toggle("MUTE", "OFF", "ON", &sel.muted, 60.f));
				// CLEAR: ALL drops every change to the selected cycle; HITS its step edits; a
				// name that one override.
				// Entries: TrackParam, kNumTrackParams + p for a random amount, -1 for the step
				// edits, -2 for the step locks.
				std::vector<int> params;
				if (sel.ownHits)
					params.push_back(-1);
				if (!sel.locks.empty())
					params.push_back(-2);
				if (sel.ownTonal)
					params.push_back(-3);
				for (int p = 0; p < kNumTrackParams; p++) {
					if (sel.has(TrackParam(p)))
						params.push_back(p);
				}
				for (int p = 0; p < kNumTrackParams; p++) {
					if (sel.hasRandom(TrackParam(p)))
						params.push_back(kNumTrackParams + p);
				}
				rows.push_back({"CLEAR", 1 + int(params.size()),
				                [params](int i) {
					                if (i == 0)
						                return std::string("ALL");
					                int e = params[i - 1];
					                if (e == -3)
						                return std::string("SCALE");
					                if (e == -2)
						                return std::string("LOCKS");
					                if (e < 0)
						                return std::string("HITS");
					                return e < kNumTrackParams ? std::string(kParamShort[e]) : "R." + std::string(kParamShort[e - kNumTrackParams]);
				                },
				                [](int) { return false; },
				                [m, params](int i) {
					                if (i == 0)
						                m->requestCycleAction(Talea::CYCLE_CLEAR_ALL);
					                else if (params[i - 1] == -3)
						                m->requestCycleAction(Talea::CYCLE_CLEAR_TONAL);
					                else if (params[i - 1] == -2)
						                m->requestCycleAction(Talea::CYCLE_CLEAR_LOCKS);
					                else if (params[i - 1] < 0)
						                m->requestCycleAction(Talea::CYCLE_CLEAR_STEPS);
					                else if (params[i - 1] >= kNumTrackParams)
						                m->requestCycleAction(Talea::CYCLE_CLEAR_RANDOM, params[i - 1] - kNumTrackParams);
					                else
						                m->requestCycleAction(Talea::CYCLE_CLEAR_PARAM, params[i - 1]);
				                },
				                params.empty() ? 40.f : kRowsEndFull});
				break;
			}
			case kPatternPage: {
				if (!module)
					break;
				Talea* m = module;
				const talea::Engine& seq = m->seq;
				auto slotName = [](int i) { return std::to_string(i + 1); };
				auto request = [m](int action) {
					return [m, action](int i) { m->patternAction = action << 8 | i; };
				};
				// Slots: orange = playing, * = holds a pattern; the queued one is outlined.
				rows.push_back({"PLAY", kNumPatterns, [&seq](int i) { return std::to_string(i + 1) + (seq.patternUsed(i) ? "*" : ""); },
				                [&seq](int i) { return seq.currentPattern() == i; }, request(Talea::PATTERN_QUEUE), kRowsEndFull});
				// COPY TO copies what plays, so with Temp on it commits the Temp state to a slot.
				rows.push_back({seq.tempActive() ? "TEMP TO" : "COPY TO", kNumPatterns, slotName, [](int) { return false; },
				                request(Talea::PATTERN_COPY), kRowsEndFull});
				rows.push_back({"CLEAR", kNumPatterns, slotName, [](int) { return false; }, request(Talea::PATTERN_CLEAR), kRowsEndFull});
				const char* timings[kNumMuteTimings] = {"NOW", "BEAT", "BAR", "PATTERN"};
				rows.push_back(choice("SWITCH", kNumMuteTimings, [=](int i) { return std::string(timings[i]); },
				                      &m->seq.patternQuantize, 80.f));
				break;
			}
			case kSongPage: {
				if (!module)
					break;
				Talea* m = module;
				const talea::Song& song = m->seq.song;
				// Rows 1-3 are the buttons and the song grid (drawSong()).
				for (int i = 0; i < 3; i++)
					rows.push_back({"", 0, [](int) { return std::string(); }, [](int) { return false; }, [](int) {}, kRowsEndFull});
				int sel = m->songSel;
				bool has = sel < song.count;
				talea::SongEntry entry = has ? song.entries[sel] : talea::SongEntry();
				rows.push_back({"PATTERN", kNumPatterns, [](int i) { return std::to_string(i + 1); },
				                [has, entry](int i) { return has && entry.slot == i; },
				                [m, has, sel](int i) {
					                if (has)
						                m->requestSong(Talea::SONG_SLOT, sel, i);
				                },
				                kRowsEndFull});
				static const int presets[] = {1, 2, 4, 8, 16, 32};
				int own = has ? m->seq.defaultBars(entry.slot) : 0;
				rows.push_back({"BARS", 10,
				                [has, entry](int i) {
					                if (i == 0)
						                return std::string("-");
					                if (i == 1)
						                return has ? std::to_string(entry.bars) : std::string("--");
					                if (i == 2)
						                return std::string("+");
					                if (i == 9)
						                return std::string("PAT");
					                return std::to_string(presets[i - 3]);
				                },
				                [has, entry, own](int i) {
					                if (!has)
						                return false;
					                if (i == 1)
						                return true;
					                if (i == 9)
						                return entry.bars == own;
					                return i >= 3 && i < 9 && entry.bars == presets[i - 3];
				                },
				                [m, has, sel, entry](int i) {
					                if (!has || i == 1)
						                return;
					                if (i == 9)
						                m->requestSong(Talea::SONG_BARS_PATTERN, sel);
					                else
						                m->requestSong(Talea::SONG_BARS, sel,
						                               i == 0 ? entry.bars - 1 : i == 2 ? entry.bars + 1 : presets[i - 3]);
				                },
				                kRowsEndFull});
				int count = song.count;
				rows.push_back({"EDIT", 4,
				                [](int i) {
					                const char* names[] = {"ADD", "INSERT", "DELETE", "CLEAR SONG"};
					                return std::string(names[i]);
				                },
				                [](int) { return false; },
				                [m, has, sel, count](int i) {
					                switch (i) {
						                case 0:
							                if (count < talea::kMaxSongEntries) {
								                m->requestSong(Talea::SONG_ADD, 0, Talea::kSongPlayingSlot);
								                m->songSel = count;
							                }
							                break;
						                case 1:
							                if (has && count < talea::kMaxSongEntries)
								                m->requestSong(Talea::SONG_INSERT, sel);
							                break;
						                case 2:
							                if (has) {
								                m->requestSong(Talea::SONG_DELETE, sel);
								                m->songSel = std::max(std::min(sel, count - 2), 0);
							                }
							                break;
						                default:
							                m->requestSong(Talea::SONG_CLEAR);
							                m->songSel = 0;
							                break;
					                }
				                },
				                110.f});
				break;
			}
			case kSetupPage: {
				if (!module)
					break;
				Talea* m = module;
				// The selected track's MIDI channel (OFF, 1-16) and the MIDI clock.
				rows.push_back({"MIDI CH", 17, [](int i) { return i == 0 ? std::string("OFF") : std::to_string(i); },
				                [m](int i) { return m->midi.channel[m->selectedTrack] == i - 1; },
				                [m](int i) { m->midi.channel[m->selectedTrack] = i - 1; }, kRowsEndFull});
				rows.push_back(toggle("CLOCK", "OFF", "ON", &m->midi.clock, 60.f));
				// Track type, and a CC track's number in tens and ones (0-119).
				rows.push_back(toggle("TYPE", "NOTE", "CC", &tr.cc, 60.f));
				if (tr.cc) {
					rows.push_back({"CC x10", kNumCcNumbers / 10, [](int i) { return std::to_string(i * 10); },
					                [&tr](int i) { return tr.ccNumber / 10 == i; },
					                [&tr](int i) { tr.ccNumber = std::min(i * 10 + tr.ccNumber % 10, kNumCcNumbers - 1); },
					                kRowsEndFull});
					rows.push_back({"CC x1", 10, [](int i) { return std::to_string(i); },
					                [&tr](int i) { return tr.ccNumber % 10 == i; },
					                [&tr](int i) { tr.ccNumber = std::min(tr.ccNumber / 10 * 10 + i, kNumCcNumbers - 1); },
					                kRowsEndSide});
				}
				break;
			}
			case kRandomPage: {
				if (!module)
					break;
				Talea* m = module;
				TrackRandom& rnd = m->random();
				// Every randomisable parameter, 5 per row: name, amount and unit. Click
				// switches the unit between per cycle loop and per Random Rate tick. In Cycle
				// edit the amounts are the edited cycle's (* = its own).
				const CycleOverrides* edited = m->cycleEdit ? &m->cycles().cycles[m->selectedCycle] : nullptr;
				std::vector<int> params;
				for (int p = 0; p < kNumTrackParams; p++) {
					if (randomisable(TrackParam(p)))
						params.push_back(p);
				}
				for (int first = 0; first < int(params.size()); first += 7) {
					std::vector<int> group(params.begin() + first, params.begin() + std::min(first + 7, int(params.size())));
					rows.push_back({first ? "" : "AMOUNT", int(group.size()),
					                [m, &rnd, edited, group](int i) {
						                int p = group[i];
						                bool own = edited && edited->hasRandom(TrackParam(p));
						                return string::f("%s %+.0f%s %c", kParamShort[p], m->randomAmount(p), own ? "*" : "",
						                                 rnd.isPerCycle(TrackParam(p)) ? 'C' : 'R');
					                },
					                [m, group](int i) { return m->randomAmount(group[i]) != 0.f; },
					                [&rnd, group](int i) { rnd.perCycle ^= 1u << group[i]; }, kRowsEndFull});
				}
				// Sequence: new seed, phase, lock, Probability phase, slew (click steps it).
				const char* seqNames[] = {"REGEN", "PHASE -", "PHASE +", "LOCK", "PROB -", "PROB +", "SLEW"};
				rows.push_back({"SEQ", 7,
				                [&rnd, seqNames](int i) {
					                return i == 6 ? string::f("SLEW %.0f%%", rnd.slew) : std::string(seqNames[i]);
				                },
				                [&rnd](int i) { return (i == 3 && rnd.locked) || (i == 6 && rnd.slew > 0.f); },
				                [m, &rnd](int i) {
					                switch (i) {
						                case 0: rnd.regenerate(); break;
						                case 1: rnd.phase--; break;
						                case 2: rnd.phase++; break;
						                case 3: m->randomLockRequest = true; break;
						                case 4: rnd.probPhase--; break;
						                case 5: rnd.probPhase++; break;
						                default: rnd.slew = rnd.slew >= 100.f ? 0.f : std::floor(rnd.slew / 25.f) * 25.f + 25.f; break;
					                }
				                },
				                kRowsEndFull});
				std::vector<int> set;
				for (int p : params) {
					if (m->randomAmount(p) != 0.f)
						set.push_back(p);
				}
				rows.push_back({"CLEAR", 1 + int(set.size()),
				                [set](int i) { return std::string(i == 0 ? "ALL" : kParamShort[set[i - 1]]); },
				                [](int) { return false; },
				                [m, set](int i) {
					                if (i == 0) {
						                for (int p : set)
							                m->clearRandom(p);
					                }
					                else
						                m->clearRandom(set[i - 1]);
				                },
				                set.empty() ? 40.f : kRowsEndFull});
				break;
			}
			case kPhrasePage: {
				const char* names[kNumPhrases] = {"CAD 1", "CAD 2", "CAD 3", "CAD 4", "SAW", "TRI", "SINE", "PULSE", "CV"};
				rows.push_back(choice("PHRASE", kNumPhrases, [=](int i) { return std::string(names[i]); }, &tr.phrase, kRowsEndSide));
				rows.push_back(choice("BARS", countOf(kGrooveBars), [](int i) { return std::to_string(kGrooveBars[i]); }, &tr.phraseLength, kRowsEndSide));
				break;
			}
			case kScalePage: {
				for (int half = 0; half < 2; half++) {
					int first = half * 12;
					rows.push_back(choice(half ? "" : "SCALE", 12, [=](int i) { return std::string(kScales[first + i].shortName); },
					                      &tr.scale, kRowsEndFull));
					rows.back().on = [&tr, first](int i) { return tr.scale == first + i; };
					rows.back().select = [&tr, first](int i) { tr.scale = first + i; };
				}
				rows.push_back(choice("ROOT", 12, [](int i) { return std::string(kNoteNames[i]); }, &tr.root, kRowsEndFull));
				break; // NOTES: scaleKeyboard()
			}
			case kStylePage: {
				const char* names[kNumStyles] = {"FIXED", "RAMP", "CLIMB", "UP/DN", "CLB U/D"};
				rows.push_back(choice("STYLE", kNumStyles, [=](int i) { return std::string(names[i]); }, &tr.style, kRowsEndSide));
				rows.push_back(toggle("PLAY", "POLY", "MONO", &tr.mono, kRowsEndSide));
				rows.push_back(toggle("VOICE", "UP", "DOWN", &tr.voicingDown, kRowsEndSide));
				break;
			}
			case kRepeatPage:
				rows.push_back(toggle("MODE", "CHOKE", "TAIL", &tr.repeatTail, 60.f));
				break;
			case kGroovePage:
				rows.push_back(choice("GROOVE", kNumGrooves, [](int i) { return std::string(kGrooveShort[i]); }, &tr.groove, kRowsEndSide));
				rows.push_back(choice("BARS", countOf(kGrooveBars), [](int i) { return std::to_string(kGrooveBars[i]); }, &tr.grooveLength, kRowsEndSide));
				rows.push_back(choice("SWING", countOf(kSwingSubdivisions), [](int i) { return string::f("x%d", kSwingSubdivisions[i]); },
				                      &tr.swingSubdivision, kRowsEndSide));
				break;
			default: break;
		}
		return rows;
	}

	math::Rect optionRect(int row, int i, const OptionRow& r) {
		float x0 = mm2px(21.f), x1 = mm2px(r.endMm);
		float w = (x1 - x0) / r.count;
		return math::Rect(x0 + i * w + 1.f, mm2px(12.5f + row * 5.2f), w - 2.f, mm2px(4.2f));
	}

	float tabWidth() {
		return box.size.x / kNumPages;
	}

	void onButton(const ButtonEvent& e) override {
		if (e.action != GLFW_PRESS)
			return;
		// STEP page lanes: click / drag sets, right-click removes.
		if (module && page == kStepPage && !muteView() && e.pos.y > mm2px(17.f)
		    && (e.button == GLFW_MOUSE_BUTTON_LEFT || e.button == GLFW_MOUSE_BUTTON_RIGHT)) {
			laneDrag = -1;
			if (editLane(e.pos, e.button == GLFW_MOUSE_BUTTON_RIGHT)) {
				if (e.button == GLFW_MOUSE_BUTTON_RIGHT)
					laneDrag = -1;
				dragPos = e.pos;
				e.consume(this);
				return;
			}
		}
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		if (e.pos.y < mm2px(4.5f)) {
			page = clamp(int(e.pos.x / tabWidth()), 0, kNumPages - 1);
			if (module && module->padMode == MODE_MUTE)
				module->leaveMuteRequest = true;
			e.consume(this);
			return;
		}
		if (!module)
			return;
		bool scratch = cycleScale();
		if (page == kKeysPage && !muteView()) {
			for (int dir : {-1, 1}) {
				if (octaveArrow(dir).contains(e.pos))
					module->keysOctave = clamp(module->keysOctave + dir, 0, kKeysOctaveMax);
			}
			if (stepPoolShown() && trackPoolButton().contains(e.pos))
				module->keyRequest = Talea::kKeyTrackPool;
			int n = keysKeyboard().keyAt(e.pos);
			if (n >= 0)
				module->keyRequest = n;
			e.consume(this);
			return;
		}
		if (page == kSongPage && !muteView() && songClick(e.pos)) {
			e.consume(this);
			return;
		}
		if (page == kScalePage && !muteView()) {
			int n = scaleKeyboard().keyAt(e.pos);
			if (n >= 0) {
				Track& tr = rowsTrack();
				toggleScaleNote(tr, n % 12);
				if (scratch)
					module->cycles().setTonal(module->selectedCycle, tonalScratch);
				e.consume(this);
				return;
			}
		}
		std::vector<OptionRow> rows = rowsFor(rowsTrack());
		for (int row = 0; row < int(rows.size()); row++) {
			for (int i = 0; i < rows[row].count; i++) {
				if (optionRect(row, i, rows[row]).contains(e.pos)) {
					rows[row].select(i);
					if (scratch)
						module->cycles().setTonal(module->selectedCycle, tonalScratch);
					e.consume(this);
					return;
				}
			}
		}
	}

	void drawRows(NVGcontext* vg, const std::vector<OptionRow>& rows) {
		nvgFontSize(vg, mm2px(2.4f));
		for (int row = 0; row < int(rows.size()); row++) {
			const OptionRow& r = rows[row];
			math::Rect first = optionRect(row, 0, r);
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			nvgFillColor(vg, labelColor);
			nvgText(vg, mm2px(3.f), first.pos.y + first.size.y / 2 + 0.5f, r.label.c_str(), nullptr);
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			for (int i = 0; i < r.count; i++) {
				math::Rect cell = optionRect(row, i, r);
				bool on = r.on(i);
				nvgBeginPath(vg);
				nvgRoundedRect(vg, cell.pos.x, cell.pos.y, cell.size.x, cell.size.y, mm2px(0.6f));
				nvgFillColor(vg, on ? nvgRGBA(0xf0, 0x8a, 0x24, 0x40) : nvgRGB(0x23, 0x26, 0x2b));
				nvgFill(vg);
				nvgFillColor(vg, on ? accent : labelColor);
				nvgText(vg, cell.pos.x + cell.size.x / 2, cell.pos.y + cell.size.y / 2 + 0.5f, r.name(i).c_str(), nullptr);
			}
		}
	}

	static std::string trackList(uint16_t mask) {
		std::string s;
		for (int i = 0; i < kNumTracks; i++) {
			if (mask & (1u << i))
				s += (s.empty() ? "" : " ") + std::to_string(i + 1);
		}
		return s;
	}

	/** "MUTE 3 7  UNMUTE 12" for the tracks a toggle mask changes. */
	std::string muteChanges(uint16_t toggles) {
		uint16_t muting = 0;
		for (int i = 0; i < kNumTracks; i++) {
			if (!module->seq.pattern.tracks[i].muted)
				muting |= uint16_t(1u << i);
		}
		std::string s;
		if (toggles & muting)
			s += "MUTE " + trackList(toggles & muting);
		if (toggles & ~muting)
			s += (s.empty() ? "" : "  ") + std::string("UNMUTE ") + trackList(toggles & ~muting);
		return s;
	}

	std::string muteInfoLine() {
		const talea::Engine& seq = module->seq;
		if (seq.stagedMutes())
			return "STAGED: " + muteChanges(seq.stagedMutes());
		if (seq.scheduledMutes()) {
			double wait = std::max(seq.scheduledMuteTime() - module->beat, 0.0);
			return string::f("ARMED: %s  IN %.1f BEATS", muteChanges(seq.scheduledMutes()).c_str(), wait);
		}
		uint16_t muted = 0;
		for (int i = 0; i < kNumTracks; i++) {
			if (seq.pattern.tracks[i].muted)
				muted |= uint16_t(1u << i);
		}
		return muted ? "MUTED: " + trackList(muted) : std::string("MUTE   NO TRACKS MUTED");
	}

	static std::string valueText(int tp, float v) {
		const ParamInfo& info = kTrackParams[tp];
		if (info.list)
			return info.list[clamp(int(v), 0, info.listSize - 1)].name;
		if (tp == TP_REPEATS && v >= info.max)
			return "∞";
		return string::f("%g", v);
	}

	/** `shown`, plus " → live" when the parameter plays at another value right now. */
	std::string withLive(int tp, const std::string& shown) {
		float live;
		if (!module || !module->liveDiffers(tp, live))
			return shown;
		return shown + ">" + liveText(tp, live);
	}

	static std::string liveText(int tp, float v) {
		switch (tp) {
			case TP_REPEATS: return v >= kTrackParams[tp].max ? "∞" : std::to_string(int(v));
			case TP_REPEAT_TIME: return kRepeatTimes[clamp(int(v), 0, countOf(kRepeatTimes) - 1)].name;
			case TP_RAMP:
			case TP_PACE:
			case TP_TIMING:
			case TP_DELAY:
			case TP_PROBABILITY: return string::f("%+.0f%%", v);
			case TP_ACCENT: return string::f("%.0f%%", v);
			default: return std::to_string(int(std::lround(v)));
		}
	}

	/** Selected steps (as they play) and the first one's locks. */
	std::string lockInfoLine(const Track& tr) {
		Talea* m = module;
		int n = tr.steps();
		std::string steps;
		for (int s = 0; s < n; s++) {
			if (hasHit(m->lockSel, tr.unrotated(s)))
				steps += (steps.empty() ? "" : " ") + std::to_string(s + 1);
		}
		// In Cycle edit * marks the cycle's own locks.
		int first = m->firstLockStep();
		std::string values;
		for (int p = 0; p < kNumTrackParams; p++) {
			float v;
			if (m->lockValue(first, p, v)) {
				bool own = m->cycleEdit && m->cycleLocks().has(first, TrackParam(p));
				values += string::f(" %s %s%s", kParamShort[p], valueText(p, v).c_str(), own ? "*" : "");
			}
		}
		if (const PoolLock* pl = m->poolLockAt(first)) {
			Track t;
			pl->apply(t);
			bool own = m->cycleEdit && m->cycleLocks().poolAt(first);
			values += " POOL " + poolText(t) + (own ? "*" : "");
		}
		const char* hint = page == kKeysPage ? "KEYS = STEP NOTES, STEP = DONE" : "CTRL+CLICK PADS, KEYS PAGE = NOTES, STEP = DONE";
		return string::f("STEPS %s:%s  (%s)", steps.c_str(), values.empty() ? " NO LOCKS" : values.c_str(), hint);
	}

	std::string infoLine(const Track& tr, int index) {
		if (module && module->armed != Talea::ARM_NONE) {
			const char* targets = "CLICK A TRACK BUTTON, KNOB, OR A STEP / CYCLE / PATTERN PAD";
			switch (module->armed) {
				case Talea::ARM_COPY: return string::f("COPY: %s", targets);
				case Talea::ARM_CLEAR: return string::f("CLEAR: %s%s", targets, module->clearSticky ? "  (STAYS ARMED)" : "");
				default: {
					int kind = module->clip.kind;
					const char* where = kind == Talea::CLIP_TRACK ? "TRACK BUTTONS"
					                    : kind == Talea::CLIP_STEP  ? "STEP PADS"
					                    : kind == Talea::CLIP_CYCLE ? "CYCLE PADS"
					                    : kind == Talea::CLIP_PATTERN ? "PATTERN PADS"
					                                                   : "TRACK BUTTONS OR CYCLE PADS";
					return string::f("PASTE %s: CLICK %s  (PASTE AGAIN TO STOP)", module->clipName().c_str(), where);
				}
			}
		}
		if (muteView())
			return muteInfoLine();
		if (module && module->lockSel && !module->rndAssign)
			return lockInfoLine(tr);
		if (pitchUnused())
			return string::f("TRACK %d  CC TRACK: PITCH SETTINGS UNUSED (VALUE = VELOCITY, GLIDE = SUSTAIN)", index + 1);
		switch (page) {
			case kCyclePage: {
				if (!module)
					return "TRACK 1  CYCLE 1 OF 1";
				const TrackCycles& cyc = module->cycles();
				int overrides = 0;
				for (int p = 0; p < kNumTrackParams; p++) {
					overrides += cyc.cycles[module->selectedCycle].has(TrackParam(p));
					overrides += cyc.cycles[module->selectedCycle].hasRandom(TrackParam(p));
				}
				overrides += cyc.cycles[module->selectedCycle].ownHits; // its step edits count as one
				overrides += !cyc.cycles[module->selectedCycle].locks.empty(); // and its step locks
				overrides += cyc.cycles[module->selectedCycle].ownTonal;        // and scale / pool
				int playing = module->seq.cycleAt(index, module->beat);
				return string::f("TRACK %d  CYCLE %d OF %d  PLAYING %d  %d OVERRIDE%s%s", index + 1, module->selectedCycle + 1,
				                 cyc.count, playing + 1, overrides, overrides == 1 ? "" : "S",
				                 module->cycleEdit ? "" : "  (CYC EDIT TO CHANGE)");
			}
			case kPatternPage: {
				if (!module)
					return "PATTERN 1";
				const talea::Engine& seq = module->seq;
				std::string s = string::f("PATTERN %d", seq.currentPattern() + 1);
				if (seq.queuedPattern() >= 0)
					s += string::f("  NEXT %d IN %.1f BEATS", seq.queuedPattern() + 1,
					               std::max(seq.patternSwitchTime() - module->beat, 0.0));
				if (seq.chainLength())
					s += "  CHAIN " + chainText();
				return s;
			}
			case kSongPage:
				return module ? songInfoLine() : "SONG OFF";
			case kSetupPage: {
				if (!module)
					return "TRACK 1  MIDI CH 1";
				int ch = module->midi.channel[index];
				return string::f("TRACK %d  %s  MIDI CH %s  OUT %s  (DEVICE: RIGHT-CLICK > MIDI OUTPUT)", index + 1,
				                 tr.cc ? string::f("CC %d", tr.ccNumber).c_str() : "NOTE",
				                 ch < 0 ? "OFF" : std::to_string(ch + 1).c_str(), module->midi.deviceName().c_str());
			}
			case kRandomPage: {
				if (!module)
					return "TRACK 1  VAR 0%  RATE 1/16";
				const TrackRandom& rnd = module->random();
				int count = 0;
				for (int p = 0; p < kNumTrackParams; p++)
					count += module->randomAmount(p) != 0.f && randomisable(TrackParam(p));
				return string::f("TRACK %d  VAR %.0f%%  RATE %s  PHASE %+d  PROB %+d%s  %d RANDOM  (C = PER CYCLE, R = PER RATE)",
				                 index + 1, rnd.variance, kRandomRates[rnd.rate].name, rnd.phase, rnd.probPhase,
				                 rnd.locked ? "  LOCKED" : "", count);
			}
			case kPhrasePage: {
				int range = tr.getInt(TP_RANGE);
				std::string amount = range >= 12 ? string::f("%d oct + %d", range / 12, range % 12) : string::f("%d st", range);
				return string::f("TRACK %d   RANGE %s   PHRASE %s", index + 1, withLive(TP_RANGE, amount).c_str(), kPhraseNames[tr.phrase]);
			}
			case kScalePage:
				return string::f("TRACK %d   %s %s   POOL %s", index + 1, kNoteNames[tr.root], kScales[tr.scale].name,
				                 poolText(tr).c_str());
			case kKeysPage:
				return string::f("TRACK %d   POOL %s   %s %s", index + 1,
				                 poolText(tr).c_str(), kNoteNames[tr.root], kScales[tr.scale].name);
			case kStylePage:
				return string::f("TRACK %d   VOICING %s   POOL %s", index + 1, withLive(TP_VOICING, std::to_string(tr.getInt(TP_VOICING))).c_str(),
				                 poolText(tr).c_str());
			case kRepeatPage: {
				int r = tr.repeats();
				return string::f("TRACK %d   REPEATS %s   TIME %s   RAMP %s   PACE %s", index + 1,
				                 withLive(TP_REPEATS, r == kInfiniteRepeats ? "∞" : std::to_string(r)).c_str(),
				                 withLive(TP_REPEAT_TIME, kRepeatTimes[tr.getInt(TP_REPEAT_TIME)].name).c_str(),
				                 withLive(TP_RAMP, string::f("%+.0f%%", tr.get(TP_RAMP))).c_str(),
				                 withLive(TP_PACE, string::f("%+.0f%%", tr.get(TP_PACE))).c_str());
			}
			case kGroovePage: {
				float p = tr.get(TP_PROBABILITY);
				std::string prob = p == 0.f ? "100%" : string::f("%.0f%% %s", 100.f - std::fabs(p), p > 0 ? "ALL" : "PULSES");
				return string::f("TRACK %d   %s %s   PROB %s   ACC %s   TIM %s   DLY %s", index + 1, tr.cc ? "VALUE" : "VEL",
				                 withLive(TP_VELOCITY, std::to_string(tr.getInt(TP_VELOCITY))).c_str(),
				                 withLive(TP_PROBABILITY, prob).c_str(),
				                 withLive(TP_ACCENT, string::f("%.0f%%", tr.get(TP_ACCENT))).c_str(),
				                 withLive(TP_TIMING, string::f("%+.0f%%", tr.get(TP_TIMING))).c_str(),
				                 withLive(TP_DELAY, string::f("%+.0f%%", tr.get(TP_DELAY))).c_str());
			}
			default: {
				// Say when Length restarts the track before all its steps have played.
				int played = tr.playedSteps();
				std::string cut = played < tr.steps() ? string::f(" (%d/%d PLAY)", played, tr.steps()) : "";
				return string::f("TRACK %d%s  STEPS %d  PULSES %s  ROT %s  %s  LEN %s%s", index + 1,
				                 tr.cc ? string::f("  CC %d", tr.ccNumber).c_str() : "", tr.steps(),
				                 withLive(TP_PULSES, std::to_string(tr.pulses())).c_str(),
				                 withLive(TP_ROTATE, std::to_string(tr.getInt(TP_ROTATE))).c_str(), kDivisions[tr.getInt(TP_DIVISION)].name,
				                 kLengths[tr.getInt(TP_LENGTH)].name, cut.c_str());
			}
		}
	}

	// ---- diagrams (right-hand area unless noted)

	void line(NVGcontext* vg, float x0, float y0, float x1, float y1, NVGcolor c, float width = 1.f) {
		nvgBeginPath(vg);
		nvgMoveTo(vg, x0, y0);
		nvgLineTo(vg, x1, y1);
		nvgStrokeColor(vg, c);
		nvgStrokeWidth(vg, width);
		nvgStroke(vg);
	}

	void cornerLabel(NVGcontext* vg, const std::string& s) {
		nvgFontSize(vg, mm2px(2.3f));
		nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		nvgFillColor(vg, labelColor);
		nvgText(vg, box.size.x - mm2px(3.f), mm2px(11.5f), s.c_str(), nullptr);
	}

	void playhead(NVGcontext* vg, double beat, double span, float x0, float x1, float top, float bottom) {
		if (beat <= 0)
			return;
		float x = x0 + (x1 - x0) * float((beat - std::floor(beat / span) * span) / span);
		line(vg, x, top, x, bottom, text);
	}

	/** Notes as a stepped line: y from the lowest to the highest (at least an octave). */
	void drawNotes(NVGcontext* vg, const std::vector<int>& notes, float x0, float x1, float top, float bottom, bool dots) {
		int lo = *std::min_element(notes.begin(), notes.end());
		int hi = std::max(*std::max_element(notes.begin(), notes.end()), lo + 12);
		auto yOf = [&](int n) { return bottom - (bottom - top) * float(n - lo) / float(hi - lo); };
		float w = (x1 - x0) / notes.size();
		for (size_t i = 0; i < notes.size(); i++) {
			float x = x0 + i * w, y = yOf(notes[i]);
			nvgBeginPath(vg);
			if (dots)
				nvgRect(vg, x + w * 0.2f, y - 1.5f, w * 0.6f, 3.f);
			else
				nvgRect(vg, x, y - 1.f, w + 0.5f, 2.f);
			nvgFillColor(vg, accent);
			nvgFill(vg);
		}
	}

	/** PHRASE: the notes Range x Phrase produces over one phrase (first pool note). */
	void drawPhrase(NVGcontext* vg, const Track& tr, double beat, double cv, int transpose) {
		double span = 4.0 * kGrooveBars[tr.phraseLength];
		float x0 = mm2px(kDiagramX), x1 = box.size.x - mm2px(3.f), top = mm2px(15.f), bottom = mm2px(32.5f);
		std::vector<int> notes;
		for (int i = 0; i < 64; i++)
			notes.push_back(tr.noteFor(0, span * i / 64, cv, transpose));
		drawNotes(vg, notes, x0, x1, top, bottom, false);
		playhead(vg, beat, span, x0, x1, top, bottom);
		int bars = kGrooveBars[tr.phraseLength];
		cornerLabel(vg, string::f("%d BAR%s", bars, bars == 1 ? "" : "S"));
	}

	/** STYLE: the next 16 notes of the Voicing/Style motion. */
	void drawStyle(NVGcontext* vg, const Track& tr) {
		float x0 = mm2px(kDiagramX), x1 = box.size.x - mm2px(3.f), top = mm2px(15.f), bottom = mm2px(32.5f);
		std::vector<int> notes;
		for (int i = 0; i < 16; i++)
			notes.push_back(tr.noteFor(i, 0.0));
		drawNotes(vg, notes, x0, x1, top, bottom, true);
		cornerLabel(vg, "NEXT 16 NOTES");
	}

	/** GROOVE: the accent curve over one groove span (dim = full shape, bright = scaled by
	Accent) with the playhead. */
	void drawGroove(NVGcontext* vg, const Track& tr, double beat) {
		bool percussion = tr.groove < GROOVE_SAW;
		double span = percussion ? 4.0 : 4.0 * kGrooveBars[tr.grooveLength];
		float x0 = mm2px(kDiagramX), x1 = box.size.x - mm2px(3.f);
		float top = mm2px(15.f), bottom = mm2px(32.5f), mid = (top + bottom) / 2;
		float amount = tr.get(TP_ACCENT) / 100.f;
		line(vg, x0, mid, x1, mid, dim);
		const int samples = 128;
		for (int pass = 0; pass < 2; pass++) {
			float scale = pass == 0 ? 1.f : amount;
			nvgBeginPath(vg);
			for (int i = 0; i <= samples; i++) {
				double t = std::min(span * i / samples, span - 1e-9);
				float x = x0 + (x1 - x0) * i / samples;
				float y = mid - float(tr.grooveValue(t)) * scale * (mid - top);
				if (i == 0)
					nvgMoveTo(vg, x, y);
				else
					nvgLineTo(vg, x, y);
			}
			nvgStrokeColor(vg, pass == 0 ? dim : accent);
			nvgStrokeWidth(vg, pass == 0 ? 1.f : 1.5f);
			nvgStroke(vg);
		}
		playhead(vg, beat, span, x0, x1, top, bottom);
		int bars = percussion ? 1 : kGrooveBars[tr.grooveLength];
		cornerLabel(vg, string::f("%d BAR%s", bars, bars == 1 ? "" : "S"));
	}

	/** REPEAT (full width): one pulse's burst, notes at their times, height = velocity,
	faint tail = gate. A line marks the track's next pulse; in Choke mode repeats past it are
	cut (drawn dim). */
	void drawRepeatBurst(NVGcontext* vg, const Track& tr) {
		const int maxDrawn = 64;
		int repeats = tr.repeats();
		int count = repeats == kInfiniteRepeats ? maxDrawn : std::min(repeats, maxDrawn);

		double times[maxDrawn + 1];
		times[0] = 0.0;
		for (int i = 1; i <= count; i++)
			times[i] = times[i - 1] + tr.repeatInterval(i - 1);

		// Distance from the first hit to the next one (or to itself coming round again).
		double nextPulse = 0.0;
		StepMask hits = tr.hits();
		int n = tr.steps();
		for (int s = 0; s < n && nextPulse == 0.0; s++) {
			if (!hasHit(hits, s))
				continue;
			for (int d = 1; d <= n; d++) {
				if (hasHit(hits, (s + d) % n)) {
					nextPulse = d * tr.stepQuarters();
					break;
				}
			}
		}
		if (repeats == kInfiniteRepeats && nextPulse > 0) {
			while (count > 0 && times[count] >= nextPulse - 1e-9)
				count--; // an infinite burst ends where the pulse comes round again
		}

		double span = std::max(times[count] + tr.repeatInterval(count), nextPulse);
		float x0 = mm2px(3.f), x1 = box.size.x - mm2px(3.f);
		float base = mm2px(32.5f), top = mm2px(18.f);
		auto xAt = [&](double t) { return x0 + float(t / span) * (x1 - x0); };

		line(vg, x0, base, x1, base, dim);
		if (nextPulse > 0) {
			float x = xAt(nextPulse);
			line(vg, x, top - mm2px(1.f), x, base, text);
			nvgFontSize(vg, mm2px(2.3f));
			nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
			nvgFillColor(vg, labelColor);
			nvgText(vg, x - mm2px(1.f), top - mm2px(1.f), "NEXT PULSE", nullptr);
		}

		for (int i = 0; i <= count; i++) {
			bool cut = !tr.repeatTail && nextPulse > 0 && times[i] >= nextPulse - 1e-9;
			int velocity = i == 0 ? tr.getInt(TP_VELOCITY) : tr.repeatVelocity(i);
			double gap = repeats != 0 ? tr.repeatInterval(i) : tr.stepQuarters();
			float x = xAt(times[i]);
			float h = (base - top) * velocity / 127.f;
			NVGcolor c = cut ? dim : (i == 0 ? text : accent);
			nvgBeginPath(vg);
			nvgRect(vg, x, base - h, std::max(xAt(times[i] + tr.gateQuarters(gap)) - x, 1.f), h);
			nvgFillColor(vg, nvgTransRGBAf(c, 0.18f));
			nvgFill(vg);
			nvgBeginPath(vg);
			nvgRect(vg, x, base - h, 2.f, h);
			nvgFillColor(vg, c);
			nvgFill(vg);
		}
	}

	/** MUTE: how-to beside the option rows, and a strip of the 16 tracks (orange = playing,
	blue-grey = muted, outlined = changing, with the state it changes to). */
	void drawMute(NVGcontext* vg) {
		const talea::Engine& seq = module->seq;
		nvgFontSize(vg, mm2px(2.3f));
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgFillColor(vg, labelColor);
		nvgText(vg, mm2px(84.f), mm2px(14.6f), "CLICK PADS TO STAGE, CLICK MUTE TO APPLY", nullptr);
		nvgText(vg, mm2px(84.f), mm2px(19.8f), "CTRL+CLICK = AT ONCE   RIGHT-CLICK MUTE = CLEAR", nullptr);

		const NVGcolor mutedFill = nvgRGB(0x26, 0x32, 0x45);
		float cw = (box.size.x - mm2px(6.f)) / kNumTracks;
		float y = mm2px(23.5f), h = mm2px(8.5f);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		for (int i = 0; i < kNumTracks; i++) {
			float x = mm2px(3.f) + i * cw;
			bool muted = seq.pattern.tracks[i].muted;
			uint16_t bit = uint16_t(1u << i);
			bool staged = seq.stagedMutes() & bit, armed = seq.scheduledMutes() & bit;
			nvgBeginPath(vg);
			nvgRoundedRect(vg, x + 1, y, cw - 2, h, mm2px(0.6f));
			nvgFillColor(vg, muted ? mutedFill : nvgTransRGBAf(accent, module->gate[i] ? 0.9f : 0.45f));
			nvgFill(vg);
			if (staged || armed) {
				nvgStrokeColor(vg, armed ? accent : text);
				nvgStrokeWidth(vg, 1.5f);
				nvgStroke(vg);
			}
			nvgFontSize(vg, mm2px(2.6f));
			nvgFillColor(vg, text);
			nvgText(vg, x + cw / 2, y + h * 0.32f, std::to_string(i + 1).c_str(), nullptr);
			nvgFontSize(vg, mm2px(2.0f));
			std::string state = staged || armed ? (muted ? "> ON" : "> OFF") : (muted ? "OFF" : "ON");
			nvgText(vg, x + cw / 2, y + h * 0.74f, state.c_str(), nullptr);
		}
	}

	/** PATTERN: outlines the queued slot (blinking) and the chain's slots; how-to beside the
	SWITCH row. */
	// ------------------------------------------------------------ SONG page

	static math::Rect mmRect(float x, float y, float w, float h) {
		return math::Rect(mm2px(Vec(x, y)), mm2px(Vec(w, h)));
	}
	/** SONG, LOOP and FOLLOW along the top row. */
	math::Rect songButton(int i) {
		const float x[] = {3.f, 27.f, 45.f}, w[] = {22.f, 16.f, 18.f};
		return mmRect(x[i], 12.5f, w[i], 4.2f);
	}
	math::Rect songArrow(int dir) {
		return mmRect(dir < 0 ? 118.f : 147.f, 12.5f, 6.f, 4.2f);
	}
	/** Grid cell `k` (0-15) of the shown page: two rows of 8 under the buttons. */
	math::Rect songCell(int k) {
		float w = (kRowsEndFull - 21.f) / kSongColumns;
		return mmRect(21.f + (k % kSongColumns) * w + 0.3f, 17.7f + (k / kSongColumns) * 5.2f, w - 0.6f, 4.2f);
	}
	/** Pages of 16 entries, with room for the + cell after the last entry. */
	int songPages() {
		return std::min(module->seq.song.count / kSongPageSize + 1, talea::kMaxSongEntries / kSongPageSize);
	}

	/** SONG page click on the buttons, arrows or grid; false when it hit none of them. */
	bool songClick(Vec pos) {
		Talea* m = module;
		if (songButton(0).contains(pos))
			m->requestSong(Talea::SONG_TOGGLE);
		else if (songButton(1).contains(pos))
			m->requestSong(Talea::SONG_LOOP);
		else if (songButton(2).contains(pos))
			m->songFollow ^= true;
		else if (songArrow(-1).contains(pos) || songArrow(1).contains(pos)) {
			int dir = songArrow(-1).contains(pos) ? -1 : 1;
			m->songPage = clamp(m->songPage + dir, 0, songPages() - 1);
			m->songFollow = false;
		}
		else {
			int count = m->seq.song.count;
			for (int k = 0; k < kSongPageSize; k++) {
				if (!songCell(k).contains(pos))
					continue;
				int idx = m->songPage * kSongPageSize + k;
				if (idx < count)
					m->songSel = idx;
				else if (idx == count && count < talea::kMaxSongEntries) {
					m->requestSong(Talea::SONG_ADD, 0, Talea::kSongPlayingSlot);
					m->songSel = count;
				}
				return true;
			}
			return false;
		}
		return true;
	}

	/** Mouse wheel over a song cell: its bars, one at a time. */
	void onHoverScroll(const HoverScrollEvent& e) override {
		if (module && page == kSongPage && !muteView() && e.scrollDelta.y != 0.f) {
			const talea::Song& song = module->seq.song;
			for (int k = 0; k < kSongPageSize; k++) {
				int idx = module->songPage * kSongPageSize + k;
				if (idx >= song.count || !songCell(k).contains(e.pos))
					continue;
				module->undoBegin();
				module->songSel = idx;
				module->requestSong(Talea::SONG_BARS, idx, song.entries[idx].bars + (e.scrollDelta.y > 0.f ? 1 : -1));
				e.consume(this);
				return;
			}
		}
		OpaqueWidget::onHoverScroll(e);
	}

	std::string songInfoLine() {
		const talea::Engine& seq = module->seq;
		const talea::Song& song = seq.song;
		std::string total = string::f("%d ENTR%s  %d BARS%s", song.count, song.count == 1 ? "Y" : "IES", song.totalBars(),
		                              song.loop ? "  LOOP" : "");
		if (!seq.songOn())
			return "SONG OFF  " + total;
		int e = seq.songEntry();
		if (e >= 0) {
			const talea::SongEntry& en = song.entries[e];
			int bar = clamp(int(std::floor((module->beat - seq.songEntryStart()) / 4.0)) + 1, 1, en.bars);
			return string::f("SONG  ENTRY %d/%d  P%d  BAR %d/%d  ", e + 1, song.count, en.slot + 1, bar, en.bars) + total;
		}
		if (seq.songPending())
			return string::f("SONG  ENTRY %d BAR %d IN %.1f BEATS  ", seq.songPendingEntry() + 1, seq.songPendingBar() + 1,
			                 std::max(seq.songPendingTime() - module->beat, 0.0))
			       + total;
		if (seq.songOverridden())
			return string::f("SONG OVERRIDDEN: CLICK SONG TO RESUME AT ENTRY %d BAR %d", seq.songResumeEntry() + 1,
			                 seq.songResumeBar() + 1);
		return song.count ? "SONG ON: STARTS ON PLAY / RESET  " + total : "SONG ON: ADD ENTRIES BELOW";
	}

	void drawSong(NVGcontext* vg) {
		Talea* m = module;
		const talea::Engine& seq = m->seq;
		const talea::Song& song = seq.song;
		bool blink = std::fmod(m->uiClock, 0.5) < 0.25;
		const NVGcolor red = nvgRGB(0xff, 0x3b, 0x30);
		int playing = seq.songEntry();
		int pending = seq.songPending() ? seq.songPendingEntry() : -1;
		bool overridden = seq.songOverridden() && !seq.songPending();
		int resume = overridden ? seq.songResumeEntry() : -1;
		int pages = songPages();
		if (m->songFollow) {
			int e = playing >= 0 ? playing : pending >= 0 ? pending : resume;
			if (e >= 0 && e < song.count)
				m->songPage = e / kSongPageSize;
		}
		m->songPage = clamp(m->songPage, 0, pages - 1);
		m->songSel = clamp(m->songSel, 0, std::max(song.count - 1, 0));

		nvgFontSize(vg, mm2px(2.4f));
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		auto box = [&](math::Rect r, bool on) {
			nvgBeginPath(vg);
			nvgRoundedRect(vg, r.pos.x, r.pos.y, r.size.x, r.size.y, mm2px(0.6f));
			nvgFillColor(vg, on ? nvgRGBA(0xf0, 0x8a, 0x24, 0x40) : nvgRGB(0x23, 0x26, 0x2b));
			nvgFill(vg);
		};
		auto label = [&](math::Rect r, const std::string& s, NVGcolor c) {
			nvgFillColor(vg, c);
			nvgText(vg, r.pos.x + r.size.x / 2, r.pos.y + r.size.y / 2 + 0.5f, s.c_str(), nullptr);
		};
		auto outline = [&](math::Rect r, NVGcolor c) {
			nvgBeginPath(vg);
			nvgRoundedRect(vg, r.pos.x, r.pos.y, r.size.x, r.size.y, mm2px(0.6f));
			nvgStrokeColor(vg, c);
			nvgStrokeWidth(vg, 1.5f);
			nvgStroke(vg);
		};

		// SONG blinks while overridden (click = resume); LOOP; FOLLOW.
		bool songLit = seq.songOn() && !(overridden && blink);
		box(songButton(0), songLit);
		label(songButton(0), seq.songOn() ? "SONG ON" : "SONG OFF", songLit ? accent : labelColor);
		box(songButton(1), song.loop);
		label(songButton(1), "LOOP", song.loop ? accent : labelColor);
		box(songButton(2), m->songFollow);
		label(songButton(2), "FOLLOW", m->songFollow ? accent : labelColor);

		// Page arrows and the entries shown.
		for (int dir : {-1, 1}) {
			math::Rect r = songArrow(dir);
			bool on = dir < 0 ? m->songPage > 0 : m->songPage < pages - 1;
			box(r, false);
			label(r, dir < 0 ? "<" : ">", on ? text : dim);
		}
		int first = m->songPage * kSongPageSize;
		label(mmRect(124.f, 12.5f, 23.f, 4.2f), string::f("%d-%d OF %d", first + 1, first + kSongPageSize, song.count), labelColor);

		// Grid rows' entry numbers.
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgFillColor(vg, labelColor);
		for (int r = 0; r < 2; r++) {
			math::Rect c = songCell(r * kSongColumns);
			nvgText(vg, mm2px(3.f), c.pos.y + c.size.y / 2 + 0.5f,
			        string::f("%d-%d", first + r * kSongColumns + 1, first + (r + 1) * kSongColumns).c_str(), nullptr);
		}
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

		// Cells: pattern and bars. Playing = orange with its progress, selected = blue outline,
		// where the song resumes = red, waiting to start = blinking orange.
		for (int k = 0; k < kSongPageSize; k++) {
			int idx = first + k;
			math::Rect r = songCell(k);
			if (idx > song.count || idx >= talea::kMaxSongEntries) {
				nvgBeginPath(vg);
				nvgRoundedRect(vg, r.pos.x, r.pos.y, r.size.x, r.size.y, mm2px(0.6f));
				nvgFillColor(vg, nvgRGB(0x16, 0x18, 0x1b));
				nvgFill(vg);
				continue;
			}
			if (idx == song.count) {
				box(r, false);
				label(r, "+", labelColor);
				continue;
			}
			const talea::SongEntry& en = song.entries[idx];
			box(r, false);
			if (idx == playing) {
				double progress = std::clamp((m->beat - seq.songEntryStart()) / (4.0 * en.bars), 0.0, 1.0);
				nvgBeginPath(vg);
				nvgRoundedRect(vg, r.pos.x, r.pos.y, r.size.x * float(progress), r.size.y, mm2px(0.6f));
				nvgFillColor(vg, nvgRGBA(0xf0, 0x8a, 0x24, 0x60));
				nvgFill(vg);
			}
			label(r, string::f("P%d x%d", en.slot + 1, en.bars), idx == playing ? text : labelColor);
			if (idx == m->songSel)
				outline(r, lockBlue);
			else if (idx == resume)
				outline(r, red);
			else if (idx == pending && blink)
				outline(r, accent);
		}

		nvgFontSize(vg, mm2px(2.3f));
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgFillColor(vg, labelColor);
		nvgText(vg, mm2px(3.f), mm2px(45.f),
		        "CLICK = SELECT   + = ADD PLAYING PATTERN   WHEEL = BARS   PATTERN PADS ADD TO THE SONG", nullptr);
	}

	void drawPattern(NVGcontext* vg, const std::vector<OptionRow>& rows) {
		const talea::Engine& seq = module->seq;
		bool blink = std::fmod(module->uiClock, 0.2) < 0.1;
		for (int i = 0; i < kNumPatterns; i++) {
			bool inChain = false;
			for (int c = 0; c < seq.chainLength(); c++)
				inChain |= seq.chainEntry(c) == i;
			bool queued = i == seq.queuedPattern() && blink;
			if (!queued && !inChain)
				continue;
			math::Rect cell = optionRect(0, i, rows[0]);
			nvgBeginPath(vg);
			nvgRoundedRect(vg, cell.pos.x, cell.pos.y, cell.size.x, cell.size.y, mm2px(0.6f));
			nvgStrokeColor(vg, queued ? accent : nvgRGB(0xff, 0x3b, 0x30));
			nvgStrokeWidth(vg, 1.5f);
			nvgStroke(vg);
		}
		nvgFontSize(vg, mm2px(2.3f));
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgFillColor(vg, labelColor);
		nvgText(vg, mm2px(84.f), mm2px(14.6f + 3 * 5.2f), "PADS: CLICK = QUEUE  SHIFT = CHAIN  CTRL = NOW", nullptr);
	}

	/** One step cell: orange hit, dark gap; faint if Length cuts it off; outlined if playing. */
	enum StepMark { MARK_NONE, MARK_LOCKED, MARK_SELECTED };

	void stepCell(NVGcontext* vg, float x, float y, float w, float h, bool hit, bool reached, bool playing, float alpha = 1.f,
	              int mark = MARK_NONE) {
		nvgBeginPath(vg);
		nvgRect(vg, x + 1, y, w - 2, h);
		NVGcolor c = reached ? (hit ? accent : nvgRGB(0x23, 0x26, 0x2b))
		                     : (hit ? nvgTransRGBAf(accent, 0.25f) : nvgRGB(0x16, 0x18, 0x1b));
		nvgFillColor(vg, nvgTransRGBAf(c, c.a * alpha));
		nvgFill(vg);
		if (mark != MARK_NONE) {
			// Locks: a light-blue bar along the bottom; selected for locking: a blue outline.
			nvgBeginPath(vg);
			nvgRect(vg, x + 1, y + h * 0.72f, w - 2, h * 0.28f);
			nvgFillColor(vg, nvgTransRGBAf(lockBlue, mark == MARK_SELECTED || hit ? 1.f : 0.5f));
			nvgFill(vg);
		}
		if (mark == MARK_SELECTED) {
			nvgBeginPath(vg);
			nvgRect(vg, x + 1, y, w - 2, h);
			nvgStrokeColor(vg, nvgRGB(0x3a, 0x8c, 0xff));
			nvgStrokeWidth(vg, 1.5f);
			nvgStroke(vg);
		}
		if (playing) {
			nvgStrokeColor(vg, text);
			nvgStrokeWidth(vg, 1.5f);
			nvgStroke(vg);
		}
	}

	/** TRACK and other pages for now: the step grid, up to 64 cells in 4 rows of 16. With
	several cycles, drawCycleLanes() instead. */
	void drawSteps(NVGcontext* vg, const Track& tr, int playStep) {
		if (module && module->cycles().count > 1) {
			drawCycleLanes(vg, tr, playStep);
			return;
		}
		StepMask hits = tr.hits();
		int played = tr.playedSteps();
		float cw = (box.size.x - mm2px(6.f)) / 16.f;
		float ch = mm2px(4.2f);
		for (int i = 0; i < tr.steps(); i++) {
			float x = mm2px(3.f) + (i % 16) * cw;
			float y = mm2px(13.5f) + (i / 16) * (ch + mm2px(0.6f));
			stepCell(vg, x, y, cw, ch, hasHit(hits, i), i < played, i == playStep, 1.f, stepMark(tr, i, -1));
		}
	}

	/** Lock mark of played step `step` of the selected track in cycle `c` (-1 = where step
	edits go). */
	int stepMark(const Track& tr, int step, int c) {
		if (!module)
			return MARK_NONE;
		int pos = tr.unrotated(step);
		if (c < 0 && hasHit(module->lockSel, pos))
			return MARK_SELECTED;
		return module->locksAt(pos, c) ? MARK_LOCKED : MARK_NONE;
	}

	/** Several cycles: the current step page (the 16 steps the pads show) as a row for what
	step clicks edit (the track, or the edited cycle), and below it a lane per cycle with the
	same steps as that cycle plays them. Lane labels: white = playing, blue-grey = muted, a
	box = selected (red while CYC EDIT). */
	void drawCycleLanes(NVGcontext* vg, const Track& tr, int playStep) {
		Talea* m = module;
		const TrackCycles& cyc = m->cycles();
		int first = m->stepPage * 16;
		int playCycle = m->seq.cycleAt(m->selectedTrack, m->beat);
		bool started = m->playing || m->beat > 0;
		float x0 = mm2px(12.f), cw = (box.size.x - mm2px(3.f) - x0) / 16.f;
		const NVGcolor red = nvgRGB(0xff, 0x3b, 0x30);

		auto label = [&](float y, float h, const std::string& s, NVGcolor c) {
			nvgFontSize(vg, std::min(mm2px(2.3f), h + mm2px(0.4f)));
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			nvgFillColor(vg, c);
			nvgText(vg, mm2px(3.f), y + h / 2 + 0.5f, s.c_str(), nullptr);
		};
		auto row = [&](const Track& t, float y, float h, int play, float alpha, int c) {
			StepMask hits = t.hits();
			int played = t.playedSteps();
			for (int i = 0; i < 16 && first + i < t.steps(); i++)
				stepCell(vg, x0 + i * cw, y, cw, h, hasHit(hits, first + i), first + i < played, first + i == play, alpha,
				         stepMark(t, first + i, c));
		};

		// Edit row
		float ch = mm2px(3.4f), y = mm2px(13.3f);
		label(y, ch, m->cycleEdit ? string::f("EDIT %d", m->selectedCycle + 1) : "TRACK", m->cycleEdit ? red : labelColor);
		bool editPlays = !m->cycleEdit || m->selectedCycle == playCycle;
		row(tr, y, ch, started && editPlays ? playStep : -1, 1.f, -1);

		// Cycle lanes
		float top = mm2px(17.6f), avail = box.size.y - mm2px(1.2f) - top;
		float gap = mm2px(0.35f);
		float lh = std::min(mm2px(3.6f), avail / cyc.count - gap);
		for (int c = 0; c < cyc.count; c++) {
			float ly = top + c * (lh + gap);
			bool playing = started && c == playCycle;
			bool muted = cyc.cycles[c].muted;
			NVGcolor lc = playing ? text : muted ? nvgRGB(0x4a, 0x5a, 0x78) : dim;
			label(ly, lh, string::f("C%d%s", c + 1, cyc.cycles[c].modified() ? "*" : ""), lc);
			if (c == m->selectedCycle) {
				nvgBeginPath(vg);
				nvgRect(vg, mm2px(2.4f), ly - 0.5f, x0 - mm2px(3.f), lh + 1.f);
				nvgStrokeColor(vg, m->cycleEdit ? red : accent);
				nvgStrokeWidth(vg, 1.f);
				nvgStroke(vg);
			}
			row(cyc.resolve(m->track(), c), ly, lh, playing ? playStep : -1, muted ? 0.35f : 1.f, c);
		}
	}

	// ---- STEP page: the current 16-step page as an edit row, and a lane per locked parameter
	// (up to kMaxLanes) with each step's lock as a bar. Click / drag in a lane sets locks,
	// right-click removes one.

	static constexpr int kMaxLanes = 6;
	int laneDrag = -1; // lane being drawn in
	Vec dragPos;

	/** Parameters locked anywhere on the edit target (the track, plus the edited cycle's
	own locks in Cycle edit). */
	std::vector<int> lockLanes() {
		uint32_t used = module->locks().used;
		if (module->cycleEdit)
			used |= module->cycleLocks().used;
		std::vector<int> lanes;
		for (int p = 0; p < kNumTrackParams; p++) {
			if (used & (1u << p))
				lanes.push_back(p);
		}
		return lanes;
	}

	float laneX0() {
		return mm2px(12.f);
	}
	float laneCellW() {
		return (box.size.x - mm2px(3.f) - laneX0()) / 16.f;
	}
	/** Lane `l` of `n`: top and height. */
	void laneGeom(int l, int n, float& y, float& h) {
		float top = mm2px(17.6f), avail = box.size.y - mm2px(1.2f) - top, gap = mm2px(0.4f);
		h = std::min(mm2px(4.2f), avail / std::max(n, 1) - gap);
		y = top + l * (h + gap);
	}

	void drawLockLanes(NVGcontext* vg, const Track& tr, int playStep) {
		Talea* m = module;
		int first = m->stepPage * 16;
		float x0 = laneX0(), cw = laneCellW();
		auto label = [&](float y, float h, const std::string& s, NVGcolor c) {
			nvgFontSize(vg, std::min(mm2px(2.3f), h + mm2px(0.4f)));
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			nvgFillColor(vg, c);
			nvgText(vg, mm2px(3.f), y + h / 2 + 0.5f, s.c_str(), nullptr);
		};
		// Edit row
		float ch = mm2px(3.4f), y = mm2px(13.3f);
		label(y, ch, m->cycleEdit ? string::f("EDIT %d", m->selectedCycle + 1) : "TRACK",
		      m->cycleEdit ? nvgRGB(0xff, 0x3b, 0x30) : labelColor);
		StepMask hits = m->seq.hits(m->selectedTrack, m->cycleEdit ? m->selectedCycle : -1);
		int played = tr.playedSteps();
		for (int i = 0; i < 16 && first + i < tr.steps(); i++)
			stepCell(vg, x0 + i * cw, y, cw, ch, hasHit(hits, first + i), first + i < played, first + i == playStep, 1.f,
			         stepMark(tr, first + i, -1));

		std::vector<int> lanes = lockLanes();
		if (lanes.empty()) {
			nvgFontSize(vg, mm2px(2.3f));
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			nvgFillColor(vg, labelColor);
			nvgText(vg, x0, mm2px(22.f), "NO LOCKS: CTRL+CLICK STEP PADS, THEN TURN A KNOB", nullptr);
			nvgText(vg, x0, mm2px(26.f), "LANES HERE: CLICK / DRAG = SET A STEP'S LOCK, RIGHT-CLICK = REMOVE", nullptr);
			return;
		}
		int n = std::min(int(lanes.size()), kMaxLanes);
		for (int l = 0; l < n; l++) {
			int p = lanes[l];
			const ParamInfo& info = kTrackParams[p];
			float ly, lh;
			laneGeom(l, n, ly, lh);
			bool more = l == n - 1 && int(lanes.size()) > n;
			label(ly, lh, string::f("%s%s", p == TP_VELOCITY && tr.cc ? "VALUE" : kParamShort[p], more ? "+" : ""), lockBlue);
			bool bipolar = info.min < 0.f;
			for (int i = 0; i < 16 && first + i < tr.steps(); i++) {
				float x = x0 + i * cw;
				nvgBeginPath(vg);
				nvgRect(vg, x + 1, ly, cw - 2, lh);
				nvgFillColor(vg, nvgRGB(0x16, 0x18, 0x1b));
				nvgFill(vg);
				float v;
				if (m->lockValue(tr.unrotated(first + i), p, v)) {
					float norm = (v - info.min) / (info.max - info.min);
					float from = bipolar ? 0.5f : 0.f;
					float lo = std::min(from, norm), hi = std::max(from, norm);
					nvgBeginPath(vg);
					nvgRect(vg, x + 1, ly + lh * (1.f - hi), cw - 2, std::max(lh * (hi - lo), 1.f));
					nvgFillColor(vg, lockBlue);
					nvgFill(vg);
				}
				if (first + i == playStep) {
					nvgBeginPath(vg);
					nvgRect(vg, x + 1, ly, cw - 2, lh);
					nvgStrokeColor(vg, text);
					nvgStrokeWidth(vg, 1.f);
					nvgStroke(vg);
				}
			}
		}
	}

	/** Lane and step under `pos` on the STEP page; false outside the lanes. */
	bool laneAt(Vec pos, int& lane, int& step, float& norm) {
		std::vector<int> lanes = lockLanes();
		int n = std::min(int(lanes.size()), kMaxLanes);
		Track tr = module->viewTrack();
		int i = int(std::floor((pos.x - laneX0()) / laneCellW()));
		if (i < 0 || i >= 16 || module->stepPage * 16 + i >= tr.steps())
			return false;
		for (int l = 0; l < n; l++) {
			float ly, lh;
			laneGeom(l, n, ly, lh);
			if (pos.y >= ly && pos.y <= ly + lh) {
				lane = lanes[l];
				step = module->stepPage * 16 + i;
				norm = clamp(1.f - (pos.y - ly) / lh, 0.f, 1.f);
				return true;
			}
		}
		return false;
	}

	/** Sets (or removes) the lock under `pos`. While drawing, the lane stays the one the drag
	started in. */
	bool editLane(Vec pos, bool remove, int onlyLane = -1) {
		int lane, step;
		float norm;
		if (onlyLane >= 0) {
			// Keep to the dragged lane: clamp the point into it.
			std::vector<int> lanes = lockLanes();
			auto it = std::find(lanes.begin(), lanes.end(), onlyLane);
			int l = int(it - lanes.begin());
			int n = std::min(int(lanes.size()), kMaxLanes);
			if (it == lanes.end() || l >= n)
				return false;
			float ly, lh;
			laneGeom(l, n, ly, lh);
			pos.y = clamp(pos.y, ly, ly + lh);
			pos.x = clamp(pos.x, laneX0(), laneX0() + laneCellW() * 16.f - 1.f);
		}
		if (!laneAt(pos, lane, step, norm))
			return false;
		const ParamInfo& info = kTrackParams[lane];
		Track tr = module->viewTrack();
		module->requestLaneEdit(tr.unrotated(step), lane, info.min + norm * (info.max - info.min), remove);
		if (onlyLane < 0)
			laneDrag = lane;
		return true;
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (laneDrag < 0 || !module)
			return;
		dragPos = dragPos.plus(e.mouseDelta.div(getAbsoluteZoom()));
		editLane(dragPos, false, laneDrag);
	}

	void onDragEnd(const DragEndEvent& e) override {
		laneDrag = -1;
	}

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, mm2px(1.f));
		nvgFillColor(args.vg, nvgRGB(0x0d, 0x0f, 0x12));
		nvgFill(args.vg);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font)
			return;
		NVGcontext* vg = args.vg;
		nvgFontFaceId(vg, font->handle);
		// A touched knob shows its page (display follows focus).
		int focus = module ? module->focusPage.exchange(-1) : -1;
		if (focus >= 0)
			page = focus;
		// Turning Rnd assign on brings up the RANDOM page, PATTERN mode the PATTERN page.
		if (module && module->rndAssign && !lastRndAssign)
			page = kRandomPage;
		lastRndAssign = module && module->rndAssign;
		bool patternMode = module && module->padMode == MODE_PATTERN;
		if (patternMode && !lastPatternMode && page != kSongPage)
			page = kPatternPage;
		if (module)
			module->songPageShown = page == kSongPage && !muteView();
		lastPatternMode = patternMode;

		// Tab strip
		float tw = tabWidth();
		float th = mm2px(4.5f);
		nvgFontSize(vg, mm2px(2.3f));
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		for (int i = 0; i < kNumPages; i++) {
			if (i == page && !muteView()) {
				nvgBeginPath(vg);
				nvgRect(vg, i * tw + 1, 1, tw - 2, th - 1);
				nvgFillColor(vg, nvgRGBA(0xf0, 0x8a, 0x24, 0x40));
				nvgFill(vg);
			}
			nvgFillColor(vg, i == page && !muteView() ? accent : labelColor);
			nvgText(vg, i * tw + tw / 2, th / 2 + 0.5f, kPageNames[i], nullptr);
		}

		Track demo;
		demo.set(TP_PULSES, 5);
		Track& base = module ? module->track() : demo;
		// Diagrams and the info line show the track as it plays in the cycle being edited.
		Track tr = module ? module->viewTrack() : demo;
		int trackIndex = module ? module->selectedTrack : 0;
		// Diagrams run on pattern time (from the playing pattern's start).
		double beat = module ? module->beat - module->seq.patternOrigin() : 0.0;

		nvgFontSize(vg, mm2px(3.2f));
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		float x = mm2px(3.f);
		if (module && module->cycleEdit && !muteView()) {
			nvgFillColor(vg, nvgRGB(0xff, 0x3b, 0x30));
			x = nvgText(vg, x, mm2px(10.5f), string::f("CYC %d EDIT  ", module->selectedCycle + 1).c_str(), nullptr);
		}
		if (module && module->seq.tempActive()) {
			nvgFillColor(vg, nvgRGB(0xff, 0xd6, 0x3a));
			x = nvgText(vg, x, mm2px(10.5f), "TEMP  ", nullptr);
		}
		if (module && module->rndAssign && !muteView()) {
			nvgFillColor(vg, accent);
			x = nvgText(vg, x, mm2px(10.5f), "RND  ", nullptr);
		}
		if (module && module->lockSel && !module->rndAssign) {
			nvgFillColor(vg, lockBlue);
			x = nvgText(vg, x, mm2px(10.5f), "LOCK  ", nullptr);
		}
		if (module && module->grouped() && !muteView()) {
			nvgFillColor(vg, nvgRGB(0xff, 0x6a, 0x5a));
			std::string tag = module->allTracks ? "ALL" : "GROUP " + trackList(module->groupMask());
			x = nvgText(vg, x, mm2px(10.5f), (tag + "  ").c_str(), nullptr);
		}
		if (module && module->armed != Talea::ARM_NONE)
			nvgFillColor(vg, nvgRGB(0x4c, 0xc9, 0xf0));
		else
		nvgFillColor(vg, text);
		nvgText(vg, x, mm2px(10.5f), infoLine(tr, trackIndex).c_str(), nullptr);

		if (module) {
			bool ext = module->inputs[Talea::CLOCK_INPUT].isConnected();
			std::string clock = ext ? string::f("EXT %d PPQN  %.1f BPM", module->ppqnIn(), module->displayBpm)
			                        : string::f("INT  %.0f BPM", module->displayBpm);
			nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
			nvgFillColor(vg, module->playing ? accent : labelColor);
			nvgText(vg, box.size.x - mm2px(3.f), mm2px(10.5f), (module->playing ? "> " + clock : clock).c_str(), nullptr);
		}

		std::vector<OptionRow> rows = rowsFor(module ? rowsTrack() : base);
		nvgGlobalAlpha(vg, pitchUnused() ? 0.35f : 1.f);
		drawRows(vg, rows);
		nvgGlobalAlpha(vg, 1.f);
		if (muteView()) {
			drawMute(vg);
			return;
		}
		switch (page) {
			case kPhrasePage:
				drawPhrase(vg, tr, beat, module ? module->seq.phraseCv[trackIndex] : 0.0, module ? module->seq.transpose[trackIndex] : 0);
				break;
			case kPatternPage:
				if (module)
					drawPattern(vg, rows);
				break;
			case kSongPage:
				if (module)
					drawSong(vg);
				break;
			case kCyclePage:
			case kRandomPage: break;
			case kKeysPage:
			case kScalePage:
				nvgGlobalAlpha(vg, pitchUnused() ? 0.35f : 1.f);
				if (page == kKeysPage)
					drawKeys(vg, tr);
				else
					drawScaleKeys(vg, module && cycleScale() ? rowsTrack() : tr);
				nvgGlobalAlpha(vg, 1.f);
				break;
			case kSetupPage: break;
			case kStylePage: drawStyle(vg, tr); break;
			case kRepeatPage: drawRepeatBurst(vg, tr); break;
			case kGroovePage: drawGroove(vg, tr, beat); break;
			case kStepPage: {
				int playStep = module && (module->playing || module->beat > 0) ? module->seq.stepAt(trackIndex, module->beat) : -1;
				if (module)
					drawLockLanes(vg, tr, playStep);
				else
					drawSteps(vg, tr, playStep);
				break;
			}
			default: {
				int playStep = module && (module->playing || module->beat > 0) ? module->seq.stepAt(trackIndex, module->beat) : -1;
				drawSteps(vg, tr, playStep);
			}
		}
	}
};

static void appendMuteMenu(Menu* menu, Talea* module) {
	menu->addChild(createMenuItem("Apply staged mutes", "", [=]() { module->muteRequest = Talea::MUTE_REQUEST_APPLY; }));
	menu->addChild(createMenuItem("Clear pending mutes", "", [=]() { module->muteRequest = Talea::MUTE_REQUEST_CLEAR; }));
	menu->addChild(createIndexPtrSubmenuItem("Mute apply timing",
		std::vector<std::string>(kMuteTimingNames, kMuteTimingNames + kNumMuteTimings), &module->seq.muteTiming));
	menu->addChild(createIndexSubmenuItem("On mute",
		{"Let notes and repeats finish", "Cut notes and repeats"},
		[=]() { return module->seq.muteCuts ? 1 : 0; },
		[=](size_t i) { module->seq.muteCuts = i == 1; }));
}

static void appendPatternMenu(Menu* menu, Talea* module) {
	menu->addChild(createIndexPtrSubmenuItem("Pattern switch timing",
		std::vector<std::string>(kMuteTimingNames, kMuteTimingNames + kNumMuteTimings), &module->seq.patternQuantize));
	menu->addChild(createMenuItem("Clear chain", "", [=]() { module->patternAction = Talea::PATTERN_CLEAR_CHAIN << 8; },
		module->seq.chainLength() == 0));
}

/** Knob of the knob groups. Under Rnd assign the ones that can't be randomised (Steps,
Division, Length) are locked and dimmed. */
struct TrackKnob : RoundSmallBlackKnob {
	bool locked() {
		Talea* m = dynamic_cast<Talea*>(module);
		return m && m->knobLocked(paramId);
	}
	bool clipClick = false; // pressed while COPY / PASTE / CLEAR was armed: no turning

	void onButton(const ButtonEvent& e) override {
		Talea* m = dynamic_cast<Talea*>(module);
		if (m && m->armed != Talea::ARM_NONE && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			m->knobClick = paramId;
			clipClick = true;
			e.consume(this);
			return;
		}
		RoundSmallBlackKnob::onButton(e);
	}
	void onDragEnd(const DragEndEvent& e) override {
		clipClick = false;
		RoundSmallBlackKnob::onDragEnd(e);
	}
	void onDragMove(const DragMoveEvent& e) override {
		if (!locked() && !clipClick)
			RoundSmallBlackKnob::onDragMove(e);
	}
	void onHoverScroll(const HoverScrollEvent& e) override {
		if (!locked())
			RoundSmallBlackKnob::onHoverScroll(e);
	}
	void onDoubleClick(const DoubleClickEvent& e) override {
		if (!locked())
			RoundSmallBlackKnob::onDoubleClick(e);
	}
	/** Modulation ring: an arc from the knob's value to where the parameter plays right now
	(random, CV), ending in a dot. */
	void drawLayer(const DrawArgs& args, int layer) override {
		RoundSmallBlackKnob::drawLayer(args, layer);
		Talea* m = dynamic_cast<Talea*>(module);
		if (layer != 1 || !m || locked())
			return;
		int tp = -1;
		for (int p = 0; p < kNumTrackParams; p++) {
			if (Talea::knobOf(p) == paramId)
				tp = p;
		}
		float live;
		if (tp < 0 || !m->liveDiffers(tp, live))
			return;
		const ParamInfo& info = kTrackParams[tp];
		auto angle = [&](float v) {
			float f = clamp((v - info.min) / (info.max - info.min), 0.f, 1.f);
			return minAngle + f * (maxAngle - minAngle) - float(M_PI) / 2.f; // nanovg: 0 = right
		};
		float a0 = angle(m->knobValue(tp)), a1 = angle(live);
		float cx = box.size.x / 2, cy = box.size.y / 2, r = box.size.x / 2 + mm2px(0.7f);
		nvgBeginPath(args.vg);
		nvgArc(args.vg, cx, cy, r, std::min(a0, a1), std::max(a0, a1), NVG_CW);
		nvgStrokeColor(args.vg, nvgRGB(0xf0, 0x8a, 0x24));
		nvgStrokeWidth(args.vg, mm2px(0.45f));
		nvgStroke(args.vg);
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, cx + r * std::cos(a1), cy + r * std::sin(a1), mm2px(0.55f));
		nvgFillColor(args.vg, nvgRGB(0xff, 0xc0, 0x70));
		nvgFill(args.vg);
	}

	void draw(const DrawArgs& args) override {
		RoundSmallBlackKnob::draw(args);
		if (!locked())
			return;
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, box.size.x / 2, box.size.y / 2, box.size.x / 2 + 1.f);
		nvgFillColor(args.vg, nvgRGBA(0x0d, 0x0f, 0x12, 0xb0));
		nvgFill(args.vg);
	}
};

/** Harmony knob: endless, and one-way, so turning back doesn't undo it (CW moves the lowest
pool note, CCW the highest). Its menu offers the pool the current run of detents started
from. */
struct HarmonyKnob : EndlessKnob {
	void appendContextMenu(Menu* menu) override {
		Talea* m = dynamic_cast<Talea*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Restore pool", "before Harmony",
			[=]() {
				// Knob menus are Rack's own param history, which this isn't: see
				// TaleaWidget::onButton, where a press on a knob cancels the module's undo.
				m->undoBegin();
				m->harmonyRestoreRequest = true;
			},
			!m->canRestorePool()));
	}
};

/** Temp button: notes Ctrl on click (keep changes); right-click offers keep / discard. */
struct TempButton : VCVLightBezel<OrangeLight> {
	void onButton(const ButtonEvent& e) override {
		Talea* m = dynamic_cast<Talea*>(module);
		if (m && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT)
			m->tempCtrl = (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL;
		VCVLightBezel<OrangeLight>::onButton(e);
	}
	void appendContextMenu(Menu* menu) override {
		Talea* m = dynamic_cast<Talea*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		bool active = m->seq.tempActive();
		menu->addChild(createMenuItem("Keep Temp changes", RACK_MOD_CTRL_NAME "+click", [=]() { m->tempRequest = Talea::TEMP_REQUEST_KEEP; }, !active));
		menu->addChild(createMenuItem("Discard Temp changes", "click", [=]() { m->tempRequest = Talea::TEMP_REQUEST_DISCARD; }, !active));
	}
};

/** CLEAR button: notes Ctrl on click (stay armed). */
struct ClearButton : VCVLightBezel<GreenBlueLight> {
	void onButton(const ButtonEvent& e) override {
		Talea* m = dynamic_cast<Talea*>(module);
		if (m && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT)
			m->clearCtrl = (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL;
		VCVLightBezel<GreenBlueLight>::onButton(e);
	}
};

/** PATTERN mode button; right-click adds the switch timing and chain actions. */
struct PatternModeButton : VCVLightBezel<OrangeLight> {
	void appendContextMenu(Menu* menu) override {
		if (Talea* m = dynamic_cast<Talea*>(module)) {
			menu->addChild(new MenuSeparator);
			appendPatternMenu(menu, m);
		}
	}
};

/** MUTE mode button; right-click adds the mute actions and settings. */
struct MuteModeButton : VCVLightBezel<OrangeLight> {
	void appendContextMenu(Menu* menu) override {
		if (Talea* m = dynamic_cast<Talea*>(module)) {
			menu->addChild(new MenuSeparator);
			appendMuteMenu(menu, m);
		}
	}
};

struct TaleaWidget : ModuleWidget {
	/** Every left / right press on the panel may edit the module: see Talea::undoBegin().
	Knob drags and knob menus are Rack's own param history, except a knob click while
	COPY / PASTE / CLEAR is armed. */
	void onButton(const ButtonEvent& e) override {
		Talea* m = getModule<Talea>();
		bool press = m && e.action == GLFW_PRESS && (e.button == GLFW_MOUSE_BUTTON_LEFT || e.button == GLFW_MOUSE_BUTTON_RIGHT);
		bool armed = m && m->armed != Talea::ARM_NONE;
		if (press)
			m->undoBegin();
		ModuleWidget::onButton(e);
		if (press && e.button == GLFW_MOUSE_BUTTON_LEFT && m->displayFollows) {
			if (app::Knob* k = dynamic_cast<app::Knob*>(e.getTarget()))
				m->focusPage = m->pageOfKnob(k->paramId);
		}
		if (press && dynamic_cast<app::Knob*>(e.getTarget()) && !(armed && e.button == GLFW_MOUSE_BUTTON_LEFT))
			m->undoCancel();
	}

	void step() override {
		ModuleWidget::step();
		if (Talea* m = getModule<Talea>())
			m->undoStep();
	}

	TaleaWidget(Talea* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Talea.svg")));
		addChild(new PanelLabels);

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		namespace L = layout;
		using M = Talea;
		using LitButton = VCVLightBezel<OrangeLight>;

		// Transport and performance
		addParam(createLightParamCentered<LitButton>(mm(L::PLAY_PARAM), module, M::PLAY_PARAM, M::PLAY_LIGHT));
		addParam(createParamCentered<VCVButton>(mm(L::RESET_PARAM), module, M::RESET_PARAM));
		addParam(createParamCentered<VCVButton>(mm(L::TAP_PARAM), module, M::TAP_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm(L::TEMPO_PARAM), module, M::TEMPO_PARAM));
		addChild(createLightCentered<MediumLight<OrangeLight>>(mm(L::CLOCK_LIGHT), module, M::CLOCK_LIGHT));
		addParam(createLightParamCentered<TempButton>(mm(L::TEMP_PARAM), module, M::TEMP_PARAM, M::TEMP_LIGHT));
		addParam(createLightParamCentered<LitButton>(mm(L::ALL_PARAM), module, M::ALL_PARAM, M::ALL_LIGHT));
		addParam(createLightParamCentered<LitButton>(mm(L::RND_ASSIGN_PARAM), module, M::RND_ASSIGN_PARAM, M::RND_ASSIGN_LIGHT));

		ContextDisplay* display = createWidget<ContextDisplay>(mm2px(Vec(L::ContextDisplay.x, L::ContextDisplay.y)));
		display->box.size = mm2px(Vec(L::ContextDisplay.w, L::ContextDisplay.h));
		display->module = module;
		addChild(display);

		// Knob groups
		struct KnobPos {
			L::Pos pos;
			int id;
		};
		const KnobPos knobs[] = {
			{L::STEPS_PARAM, M::STEPS_PARAM}, {L::PULSES_PARAM, M::PULSES_PARAM},
			{L::ROTATE_PARAM, M::ROTATE_PARAM}, {L::DIVISION_PARAM, M::DIVISION_PARAM},
			{L::LENGTH_PARAM, M::LENGTH_PARAM},
			{L::REPEATS_PARAM, M::REPEATS_PARAM}, {L::RAMP_PARAM, M::RAMP_PARAM},
			{L::REPEAT_TIME_PARAM, M::REPEAT_TIME_PARAM}, {L::PACE_PARAM, M::PACE_PARAM},
			{L::VELOCITY_PARAM, M::VELOCITY_PARAM}, {L::PROBABILITY_PARAM, M::PROBABILITY_PARAM},
			{L::SUSTAIN_PARAM, M::SUSTAIN_PARAM}, {L::ACCENT_PARAM, M::ACCENT_PARAM},
			{L::TIMING_PARAM, M::TIMING_PARAM}, {L::DELAY_PARAM, M::DELAY_PARAM},
			{L::PITCH_PARAM, M::PITCH_PARAM}, {L::VOICING_PARAM, M::VOICING_PARAM},
			{L::RANGE_PARAM, M::RANGE_PARAM},
			{L::VARIANCE_PARAM, M::VARIANCE_PARAM}, {L::RANDOM_RATE_PARAM, M::RANDOM_RATE_PARAM},
		};
		for (const KnobPos& k : knobs)
			addParam(createParamCentered<TrackKnob>(mm(k.pos), module, k.id));
		addParam(createParamCentered<HarmonyKnob>(mm(L::HARMONY_PARAM), module, M::HARMONY_PARAM));

		// Pad modes and edit actions
		const L::Pos modePos[kNumModes] = {{}, L::MODE_STEP_PARAM, L::MODE_CYCLE_PARAM,
		                                   {}, L::MODE_MUTE_PARAM, L::MODE_PATTERN_PARAM};
		for (int i = MODE_STEP; i < kNumModes; i++) {
			if (i == MODE_KEYS) // retired: the KEYS page tab replaced it
				continue;
			if (i == MODE_MUTE)
				addParam(createLightParamCentered<MuteModeButton>(mm(modePos[i]), module, M::MODE_TRACK_PARAM + i, M::MODE_LIGHT + i));
			else if (i == MODE_PATTERN)
				addParam(createLightParamCentered<PatternModeButton>(mm(modePos[i]), module, M::MODE_TRACK_PARAM + i, M::MODE_LIGHT + i));
			else
				addParam(createLightParamCentered<LitButton>(mm(modePos[i]), module, M::MODE_TRACK_PARAM + i, M::MODE_LIGHT + i));
		}
		addParam(createLightParamCentered<VCVLightBezel<RedLight>>(mm(L::CYCLE_EDIT_PARAM), module, M::CYCLE_EDIT_PARAM, M::CYCLE_EDIT_LIGHT));
		addParam(createLightParamCentered<VCVLightBezel<GreenBlueLight>>(mm(L::COPY_PARAM), module, M::COPY_PARAM, M::COPY_LIGHT));
		addParam(createLightParamCentered<VCVLightBezel<GreenBlueLight>>(mm(L::PASTE_PARAM), module, M::PASTE_PARAM, M::PASTE_LIGHT));
		addParam(createLightParamCentered<ClearButton>(mm(L::CLEAR_PARAM), module, M::CLEAR_PARAM, M::CLEAR_LIGHT));
		for (int i = 0; i < kNumStepPages; i++)
			addParam(createLightParamCentered<SmallLightButton>(mm(L::STEP_PAGE_PARAM[i]), module, M::STEP_PAGE_PARAM + i, M::STEP_PAGE_LIGHT + i));
		addParam(createLightParamCentered<SmallLightButton>(mm(L::FOLLOW_PARAM), module, M::FOLLOW_PARAM, M::FOLLOW_LIGHT));

		for (int i = 0; i < kNumPads; i++) {
			PadButton* pad = createParamCentered<PadButton>(mm(L::PAD_PARAM[i]), module, M::PAD_PARAM + i);
			pad->firstLightId = M::PAD_LIGHT + 3 * i;
			if (module) {
				pad->ctrlFlag = &module->padCtrl[i];
				pad->shiftFlag = &module->padShift[i];
			}
			addParam(pad);
		}
		for (int i = 0; i < kNumTracks; i++) {
			TrackButton* b = createParamCentered<TrackButton>(mm(L::TRACK_SELECT_PARAM[i]), module, M::TRACK_SELECT_PARAM + i);
			b->firstLightId = M::TRACK_SELECT_LIGHT + 3 * i;
			b->index = i;
			if (module) {
				b->selectedTrack = &module->selectedTrack;
				b->ctrlFlag = &module->trackCtrl[i];
				b->shiftFlag = &module->trackShift[i];
			}
			addParam(b);
		}

		// Inputs and outputs (the other inputs are on the IN expander)
		addInput(createInputCentered<PJ301MPort>(mm(L::CLOCK_INPUT), module, M::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm(L::RESET_INPUT), module, M::RESET_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm(L::RUN_INPUT), module, M::RUN_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm(L::PITCH_OUTPUT), module, M::PITCH_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm(L::GATE_OUTPUT), module, M::GATE_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm(L::VELOCITY_OUTPUT), module, M::VELOCITY_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm(L::CLOCK_OUTPUT), module, M::CLOCK_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm(L::RESET_OUTPUT), module, M::RESET_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm(L::EOP_OUTPUT), module, M::EOP_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Talea* module = getModule<Talea>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Clock"));
		std::vector<std::string> ppqn;
		for (int p : kPpqnChoices)
			ppqn.push_back(string::f("%d PPQN", p));
		menu->addChild(createIndexPtrSubmenuItem("Clock input resolution", ppqn, &module->ppqnInIndex));
		menu->addChild(createIndexPtrSubmenuItem("Clock output resolution", ppqn, &module->ppqnOutIndex));
		menu->addChild(createIndexSubmenuItem("Run input", {"Gate (run while high)", "Trigger (toggle)"},
			[=]() { return module->runGateMode ? 0 : 1; },
			[=](size_t i) { module->runGateMode = i == 0; }));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("MIDI"));
		menu->addChild(createSubmenuItem("MIDI output", module->midi.deviceName(), [=](Menu* sub) {
			appendMidiMenu(sub, &module->midi.port);
		}));
		menu->addChild(createBoolPtrMenuItem("Send MIDI clock (24 PPQN, start / stop)", "", &module->midi.clock));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Display"));
		menu->addChild(createBoolPtrMenuItem("Display follows knobs", "", &module->displayFollows));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Mute"));
		appendMuteMenu(menu, module);
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Pattern"));
		appendPatternMenu(menu, module);
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(string::f("Track %d", module->selectedTrack + 1)));
		menu->addChild(createIndexSubmenuItem("Repeat mode",
			{"Choke (next pulse cuts repeats)", "Tail (repeats overlap)"},
			[=]() { return module->track().repeatTail ? 1 : 0; },
			[=](size_t i) { module->track().repeatTail = i == 1; }));
		std::vector<std::string> grooves(kGrooveNames, kGrooveNames + kNumGrooves), bars, swing;
		for (int b : kGrooveBars)
			bars.push_back(string::f("%d bar%s", b, b == 1 ? "" : "s"));
		for (int s : kSwingSubdivisions)
			swing.push_back(string::f("x%d", s));
		menu->addChild(createIndexPtrSubmenuItem("Groove (Accent shape)", grooves, &module->track().groove));
		menu->addChild(createIndexPtrSubmenuItem("Groove length (waveforms)", bars, &module->track().grooveLength));
		menu->addChild(createIndexPtrSubmenuItem("Timing subdivision", swing, &module->track().swingSubdivision));
		std::vector<std::string> scales, roots, phrases;
		for (const ScaleInfo& sc : kScales)
			scales.push_back(sc.name);
		for (const char* n : kNoteNames)
			roots.push_back(n);
		for (const char* p : kPhraseNames)
			phrases.push_back(p);
		Track& tr = module->track();
		menu->addChild(createIndexPtrSubmenuItem("Scale", scales, &tr.scale));
		menu->addChild(createIndexPtrSubmenuItem("Root", roots, &tr.root));
		menu->addChild(createIndexPtrSubmenuItem("Style", std::vector<std::string>(kStyleNames, kStyleNames + kNumStyles), &tr.style));
		menu->addChild(createBoolPtrMenuItem("Mono (arpeggiate the pool)", "", &tr.mono));
		menu->addChild(createBoolPtrMenuItem("Voicing moves down", "", &tr.voicingDown));
		menu->addChild(createIndexPtrSubmenuItem("Phrase", phrases, &tr.phrase));
		menu->addChild(createIndexPtrSubmenuItem("Phrase length", bars, &tr.phraseLength));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Kill stuck notes", "", [=]() { module->killRequested = true; }));
	}
};

Model* modelTalea = createModel<Talea, TaleaWidget>("Talea");
