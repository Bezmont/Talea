#pragma once
#include "core/Model.hpp"

/** Talea -> Talea OUT, once per sample through Rack's expander messages (the OUT module
owns the two buffers on its left side). */
struct OutMessage {
	int channels[talea::kNumTracks] = {}; // chord notes of the latest note-on, 1..kMaxPool
	float pitch[talea::kNumTracks][talea::kMaxPool] = {}; // V/oct, 0 V = C4
	bool gate[talea::kNumTracks] = {};
	float velocity[talea::kNumTracks] = {}; // 0-10 V
	bool muted[talea::kNumTracks] = {};
};

/** Knob parameters the IN expander modulates, in its jack order (all but Steps, Division and
Length, which set the loop length). */
constexpr int kNumModParams = 15;
inline constexpr talea::TrackParam kModParams[kNumModParams] = {
	talea::TP_PULSES, talea::TP_ROTATE, talea::TP_REPEATS, talea::TP_RAMP, talea::TP_REPEAT_TIME,
	talea::TP_PACE, talea::TP_VELOCITY, talea::TP_PROBABILITY, talea::TP_SUSTAIN, talea::TP_ACCENT,
	talea::TP_TIMING, talea::TP_DELAY, talea::TP_PITCH, talea::TP_VOICING, talea::TP_RANGE,
};

/** Talea IN -> Talea, once per sample (Talea owns the two buffers on its left side). Poly
inputs arrive spread per track: channel n = track n, a mono cable on every track. */
struct InMessage {
	float cv[kNumModParams][talea::kNumTracks] = {}; // volts x attenuverter
	float transpose[talea::kNumTracks] = {};         // V/oct
	float phrase[talea::kNumTracks] = {};            // volts
	bool patternOn = false;
	float pattern = 0.f; // 0-10 V
	float temp = 0.f;
	bool muteOn[talea::kNumTracks] = {}; // a mute gate is patched for the track
	float mute[talea::kNumTracks] = {};
	float muteApply = 0.f;
	float regen[talea::kNumTracks] = {};
};
