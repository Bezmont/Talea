#pragma once
#include <rack.hpp>
#include "core/Model.hpp"

/** Talea -> Talea OUT, once per sample through Rack's expander messages (the OUT module
owns the two buffers on its left side). */
struct OutMessage {
	// Per voice (OUT channel): pitch and velocity hold after the voice's gate falls.
	float pitch[talea::kNumTracks][talea::kMaxPool] = {};    // V/oct, 0 V = C4
	bool voiceGate[talea::kNumTracks][talea::kMaxPool] = {};
	float velocity[talea::kNumTracks][talea::kMaxPool] = {}; // 0-10 V
	bool gate[talea::kNumTracks] = {}; // the track's gate (status light)
	bool muted[talea::kNumTracks] = {};
};

/** What Talea reads of the OUT expander: its channel count per output, so voices are
allocated only on channels that exist. */
struct TaleaOutBase : rack::engine::Module {
	int polyphony = talea::kMaxPool;
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
