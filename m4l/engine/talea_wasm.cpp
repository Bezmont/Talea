// A plain C interface to Talea's engine, for the WebAssembly build that Max's v8 object
// loads (see build.py). Every function takes and returns numbers or C strings, so the
// script can call it through Emscripten's cwrap.
//
// One engine per brain device. Events from the latest talea_process() call stay in a
// buffer the script reads with the talea_event_* functions, until the next call.

#include "core/Engine.hpp"
#include "core/Params.hpp"
#include "core/State.hpp"
#include "core/third_party/nlohmann/json.hpp"

#include <emscripten/emscripten.h>

#include <memory>
#include <string>

using namespace talea;

namespace {

// Saved playback states, one per step the brain computes ahead, reused in turn: enough
// for the longest look-ahead (16 steps) with room to spare.
constexpr int kPlaybackSlots = 32;

struct Host {
	Engine engine;
	EventList events;
	std::string text; // the latest JSON handed back to the script
	Engine::Playback saved[kPlaybackSlots];
};

const NoteEvent* event(Host* h, int i) {
	return i >= 0 && i < h->events.size ? &h->events.events[i] : nullptr;
}

} // namespace

extern "C" {

// An engine is about 0.8 MB, too big for the WebAssembly stack: it lives on the heap.
EMSCRIPTEN_KEEPALIVE Host* talea_create() {
	return new Host();
}

EMSCRIPTEN_KEEPALIVE void talea_destroy(Host* h) {
	delete h;
}

EMSCRIPTEN_KEEPALIVE int talea_num_tracks() {
	return kNumTracks;
}

EMSCRIPTEN_KEEPALIVE int talea_num_params() {
	return kNumTrackParams;
}

/** The parameter table, so the device can build its controls from it. */
EMSCRIPTEN_KEEPALIVE const char* talea_param_key(int p) {
	return p >= 0 && p < kNumTrackParams ? kTrackParams[p].key : "";
}

EMSCRIPTEN_KEEPALIVE const char* talea_param_name(int p) {
	return p >= 0 && p < kNumTrackParams ? kTrackParams[p].name : "";
}

EMSCRIPTEN_KEEPALIVE float talea_param_min(int p) {
	return p >= 0 && p < kNumTrackParams ? kTrackParams[p].min : 0.f;
}

EMSCRIPTEN_KEEPALIVE float talea_param_max(int p) {
	return p >= 0 && p < kNumTrackParams ? kTrackParams[p].max : 0.f;
}

EMSCRIPTEN_KEEPALIVE float talea_param_default(int p) {
	return p >= 0 && p < kNumTrackParams ? kTrackParams[p].def : 0.f;
}

/** A track's own value of a parameter (cycle overrides move with it, as in Rack). */
EMSCRIPTEN_KEEPALIVE void talea_set_param(Host* h, int track, int p, float v) {
	if (track >= 0 && track < kNumTracks && p >= 0 && p < kNumTrackParams)
		h->engine.pattern.setBase(track, TrackParam(p), v);
}

EMSCRIPTEN_KEEPALIVE float talea_get_param(Host* h, int track, int p) {
	if (track >= 0 && track < kNumTracks && p >= 0 && p < kNumTrackParams)
		return h->engine.pattern.tracks[track].get(TrackParam(p));
	return 0.f;
}

/** Replaces the playing pattern with one saved by Rack's Talea (the "core" string of a
pattern slot). Returns 0 if the text isn't usable. */
EMSCRIPTEN_KEEPALIVE int talea_load_pattern(Host* h, const char* json) {
	Pattern p;
	if (!patternFromJson(json ? json : "", p))
		return 0;
	h->engine.storePattern(h->engine.currentPattern(), p);
	return 1;
}

EMSCRIPTEN_KEEPALIVE const char* talea_save_pattern(Host* h) {
	h->text = patternToJson(h->engine.committedPattern());
	return h->text.c_str();
}

/** Works out the events in (from, to], in quarter notes since the start of the song, and
returns how many there are. */
EMSCRIPTEN_KEEPALIVE int talea_process(Host* h, double from, double to) {
	h->events.clear();
	h->engine.process(from, to, h->events);
	return h->events.size;
}

/** Note-offs at `time` for every sounding note. Returns the event count, as above. */
EMSCRIPTEN_KEEPALIVE int talea_all_notes_off(Host* h, double time) {
	h->events.clear();
	h->engine.allNotesOff(time, h->events);
	return h->events.size;
}

/** Saves the engine's playback state (sounding notes, repeats to come, the window
position) in slot `slot` of kPlaybackSlots, e.g. after each step. */
EMSCRIPTEN_KEEPALIVE void talea_save_playback(Host* h, int slot) {
	if (slot >= 0 && slot < kPlaybackSlots)
		h->engine.savePlayback(h->saved[slot]);
}

/** Puts the engine back to a state saved with talea_save_playback(), to compute again from
there after an edit. */
EMSCRIPTEN_KEEPALIVE void talea_load_playback(Host* h, int slot) {
	if (slot >= 0 && slot < kPlaybackSlots)
		h->engine.loadPlayback(h->saved[slot]);
}

EMSCRIPTEN_KEEPALIVE int talea_playback_slots() {
	return kPlaybackSlots;
}

/** Back to the start: the playing pattern restarts at time 0. */
EMSCRIPTEN_KEEPALIVE void talea_rewind(Host* h) {
	h->engine.rewind();
}

EMSCRIPTEN_KEEPALIVE void talea_set_muted(Host* h, int track, int muted, double time) {
	if (track >= 0 && track < kNumTracks) {
		h->events.clear();
		h->engine.setMuted(track, muted != 0, time, h->events);
	}
}

// ---- the step pads -------------------------------------------------------------------

static bool validTrack(int track) {
	return track >= 0 && track < kNumTracks;
}

/** A pad click in Step mode: turns played step `step` of the track off or on, without
moving any other hit (Track::toggleStep, as Rack's pads). */
EMSCRIPTEN_KEEPALIVE void talea_toggle_step(Host* h, int track, int step) {
	if (validTrack(track) && step >= 0 && step < kMaxSteps)
		h->engine.pattern.tracks[track].toggleStep(step);
}

/** The track's Steps, and how many of them play before Length cuts the loop off. */
EMSCRIPTEN_KEEPALIVE int talea_steps(Host* h, int track) {
	return validTrack(track) ? h->engine.pattern.tracks[track].steps() : 0;
}

EMSCRIPTEN_KEEPALIVE int talea_played_steps(Host* h, int track) {
	return validTrack(track) ? h->engine.pattern.tracks[track].playedSteps() : 0;
}

/** The track's hits (a bit per played step), in two 32-bit halves: steps 0-31 and 32-63.
As doubles, so the top bit doesn't come back as a sign. */
EMSCRIPTEN_KEEPALIVE double talea_hits_low(Host* h, int track) {
	return validTrack(track) ? double(uint32_t(h->engine.hits(track) & 0xffffffffu)) : 0.0;
}

EMSCRIPTEN_KEEPALIVE double talea_hits_high(Host* h, int track) {
	return validTrack(track) ? double(uint32_t(h->engine.hits(track) >> 32)) : 0.0;
}

/** The track's step at time `t` (quarter notes since the start of the song), or -1 before
the start. */
EMSCRIPTEN_KEEPALIVE int talea_step_at(Host* h, int track, double t) {
	return validTrack(track) ? h->engine.stepAt(track, t) : -1;
}

// The events of the latest call, by index.
EMSCRIPTEN_KEEPALIVE double talea_event_time(Host* h, int i) {
	const NoteEvent* e = event(h, i);
	return e ? e->time : 0.0;
}

EMSCRIPTEN_KEEPALIVE int talea_event_track(Host* h, int i) {
	const NoteEvent* e = event(h, i);
	return e ? e->track : -1;
}

EMSCRIPTEN_KEEPALIVE int talea_event_on(Host* h, int i) {
	const NoteEvent* e = event(h, i);
	return e && e->on ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int talea_event_note(Host* h, int i) {
	const NoteEvent* e = event(h, i);
	return e ? e->note : 0;
}

EMSCRIPTEN_KEEPALIVE int talea_event_velocity(Host* h, int i) {
	const NoteEvent* e = event(h, i);
	return e ? e->velocity : 0;
}

EMSCRIPTEN_KEEPALIVE int talea_event_chord_size(Host* h, int i) {
	const NoteEvent* e = event(h, i);
	return e ? e->chordSize : 0;
}

EMSCRIPTEN_KEEPALIVE int talea_event_chord_note(Host* h, int i, int j) {
	const NoteEvent* e = event(h, i);
	return e && j >= 0 && j < e->chordSize ? e->chord[j] : 0;
}

EMSCRIPTEN_KEEPALIVE int talea_event_cc(Host* h, int i) {
	const NoteEvent* e = event(h, i);
	return e && e->cc ? e->ccNumber : -1;
}

EMSCRIPTEN_KEEPALIVE double talea_event_glide(Host* h, int i) {
	const NoteEvent* e = event(h, i);
	return e ? e->glide : 0.0;
}

// ---- mutes and pattern switches --------------------------------------------------------
//
// `now` is the host time the change is made at: the end of the step playing (strictly
// after it, the boundary a queued change waits for). Changes that cut sounding notes (a
// switch at once) leave the note-offs in the events buffer; the script ends its notes
// itself.

/** The host's time jumped: the next window starts at `t` (Engine::jump). */
EMSCRIPTEN_KEEPALIVE void talea_jump(Host* h, double t) {
	h->engine.jump(t);
}

/** Commits mute changes (a bit per track to toggle) for the next mute boundary after `now`. */
EMSCRIPTEN_KEEPALIVE void talea_commit_mutes(Host* h, int toggle, double now) {
	for (int i = 0; i < kNumTracks; i++) {
		if (toggle & (1 << i))
			h->engine.stageMute(i);
	}
	h->engine.commitMutes(now);
}

/** Queues pattern `slot` (click), adds it to the chain (`chain`), or switches at once
(`immediate`; at time 0 if now < 0, as when stopped). */
EMSCRIPTEN_KEEPALIVE void talea_select_pattern(Host* h, int slot, int chain, int immediate, double now) {
	if (slot < 0 || slot >= kNumPatterns)
		return;
	h->events.clear();
	if (chain)
		h->engine.chainPattern(slot, now, h->events, immediate != 0);
	else
		h->engine.queuePattern(slot, now, h->events, immediate != 0);
}

/** What the pads show: 0 muted tracks (bits), 1 committed mute changes waiting (bits),
2 the playing pattern, 3 the queued one (-1: none), 4 slots holding a pattern (bits),
5 chain length, 6 + i chain entry i. */
EMSCRIPTEN_KEEPALIVE int talea_perf(Host* h, int what) {
	const Engine& e = h->engine;
	switch (what) {
		case 0: {
			int m = 0;
			for (int i = 0; i < kNumTracks; i++)
				m |= e.pattern.tracks[i].muted ? 1 << i : 0;
			return m;
		}
		case 1: return e.scheduledMutes();
		case 2: return e.currentPattern();
		case 3: return e.queuedPattern();
		case 4: {
			int m = 0;
			for (int i = 0; i < kNumPatterns; i++)
				m |= e.patternUsed(i) ? 1 << i : 0;
			return m;
		}
		case 5: return e.chainLength();
		default: return what - 6 < e.chainLength() ? e.chainEntry(what - 6) : -1;
	}
}

/** The whole bank as JSON, as Rack's Talea saves it: {"patterns": [{"slot", "core"}...]
(the slots holding something), "currentPattern"}. */
EMSCRIPTEN_KEEPALIVE const char* talea_save_bank(Host* h) {
	nlohmann::json root, patterns = nlohmann::json::array();
	for (int i = 0; i < kNumPatterns; i++) {
		if (h->engine.patternUsed(i))
			patterns.push_back({{"slot", i}, {"core", patternToJson(h->engine.patternSlot(i))}});
	}
	root["patterns"] = patterns;
	root["currentPattern"] = h->engine.currentPattern();
	h->text = root.dump();
	return h->text.c_str();
}

/** Replaces the bank with one from talea_save_bank(); the queue, chain and mute changes
waiting are dropped. Returns 0 (changing nothing) if the text isn't usable. */
EMSCRIPTEN_KEEPALIVE int talea_load_bank(Host* h, const char* text) {
	nlohmann::json root = nlohmann::json::parse(text ? text : "", nullptr, false);
	if (root.is_discarded() || !root.is_object() || !root.contains("patterns") || !root["patterns"].is_array())
		return 0;
	// A Pattern is ~0.4 MB: on the heap, not the WebAssembly stack.
	std::vector<std::unique_ptr<Pattern>> slots(kNumPatterns);
	for (const auto& slot : root["patterns"]) {
		if (!slot.is_object() || !slot.contains("slot") || !slot["slot"].is_number_integer() ||
		    !slot.contains("core") || !slot["core"].is_string())
			return 0;
		int n = slot["slot"].get<int>();
		if (n < 0 || n >= kNumPatterns)
			return 0;
		slots[n].reset(new Pattern);
		if (!patternFromJson(slot["core"].get<std::string>(), *slots[n]))
			return 0;
	}
	int current = 0;
	if (root.contains("currentPattern") && root["currentPattern"].is_number_integer())
		current = std::max(0, std::min(kNumPatterns - 1, root["currentPattern"].get<int>()));
	h->engine.resetBank();
	for (int i = 0; i < kNumPatterns; i++) {
		if (slots[i])
			h->engine.storePattern(i, *slots[i]);
	}
	h->engine.setCurrentPattern(current);
	return 1;
}

} // extern "C"
