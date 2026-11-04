#pragma once
#include "core/Euclid.hpp"
#include "core/Values.hpp"

// The single per-track parameter table (core rule 5). Hosts build their controls from it:
// Rack configures its knobs from these entries; a plugin will build its parameter list.
// Values are stored in display units (e.g. -100..100 for a bipolar %); list parameters
// store the list index.

namespace talea {

enum TrackParam {
	TP_STEPS,
	TP_PULSES,
	TP_ROTATE,
	TP_DIVISION,
	TP_LENGTH,
	TP_REPEATS,
	TP_RAMP,
	TP_REPEAT_TIME,
	TP_PACE,
	TP_VELOCITY,
	TP_PROBABILITY,
	TP_SUSTAIN,
	TP_ACCENT,
	TP_TIMING,
	TP_DELAY,
	TP_PITCH,
	TP_VOICING,
	TP_RANGE,
	kNumTrackParams
};

struct ParamInfo {
	const char* key;  // stable identifier used in saved state; never rename
	const char* name; // display name
	float min, max, def;
	const char* unit;
	bool snap;
	const NamedValue* list = nullptr; // quantized list; value = index
	int listSize = 0;
};

#define TALEA_LIST(values) values, countOf(values)

inline constexpr ParamInfo kTrackParams[kNumTrackParams] = {
	{"steps", "Steps", 1, kMaxSteps, 16, "", true},
	{"pulses", "Pulses", 0, kMaxSteps, 0, "", true},
	{"rotate", "Rotate", 0, kMaxSteps - 1, 0, "", true},
	{"division", "Division", 0, countOf(kDivisions) - 1, kDefaultDivision, "", true, TALEA_LIST(kDivisions)},
	{"length", "Length", 0, countOf(kLengths) - 1, kDefaultLength, "", true, TALEA_LIST(kLengths)},
	{"repeats", "Repeats", 0, kMaxRepeats + 1, 0, "", true},
	{"ramp", "Ramp", -100, 100, 0, "%", false},
	{"repeatTime", "Repeat time", 0, countOf(kRepeatTimes) - 1, kDefaultRepeatTime, "", true, TALEA_LIST(kRepeatTimes)},
	{"pace", "Pace", -100, 100, 0, "%", false},
	{"velocity", "Velocity", 0, 127, 100, "", true},
	{"probability", "Probability (CW: all notes, CCW: pulses only)", -100, 100, 0, "%", false},
	{"sustain", "Sustain", 0, 100, 50, "%", false},
	{"accent", "Accent", 0, 100, 0, "%", false},
	{"timing", "Timing (swing)", -100, 100, 0, "%", false},
	{"delay", "Delay", -100, 100, 0, "%", false},
	{"pitch", "Pitch", -48, 48, 0, " st", true},
	{"voicing", "Voicing", 0, 16, 0, "", true},
	{"range", "Range", 0, 48, 0, " st", true},
};

#undef TALEA_LIST

} // namespace talea
