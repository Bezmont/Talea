// A plain C interface to Talea's engine, for the WebAssembly build that Max's v8 object
// loads (see build.py). Every function takes and returns numbers or C strings, so the
// script can call it through Emscripten's cwrap.
//
// One engine per brain device. Events from the latest talea_process() call stay in a
// buffer the script reads with the talea_event_* functions, until the next call.

#include "core/Engine.hpp"
#include "core/Params.hpp"
#include "core/State.hpp"
#include "core/Tonal.hpp"
#include "core/third_party/nlohmann/json.hpp"

#include <algorithm>

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

EMSCRIPTEN_KEEPALIVE void talea_set_setting(Host* h, int id, int v);

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
	root["muteTiming"] = h->engine.muteTiming;
	root["muteCuts"] = h->engine.muteCuts;
	root["patternQuantize"] = h->engine.patternQuantize;
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
	auto setting = [&](const char* key, int id) {
		if (root.contains(key) && (root[key].is_number_integer() || root[key].is_boolean()))
			talea_set_setting(h, id, root[key].is_boolean() ? int(root[key].get<bool>()) : root[key].get<int>());
	};
	setting("muteTiming", 0);
	setting("muteCuts", 1);
	setting("patternQuantize", 2);
	return 1;
}

} // extern "C"

// ---- the display's settings ------------------------------------------------------------

namespace {

/** A track setting that isn't a dial, by number (the display's rows; jezric_talea_core.js
names them): its range and where it lives. */
struct Field {
	int count; // values 0 .. count-1
	int Track::*i;
	bool Track::*b;
};

const Field kFields[] = {
	{kNumStyles, &Track::style, nullptr},            // 0 style
	{2, nullptr, &Track::mono},                      // 1 mono
	{2, nullptr, &Track::voicingDown},               // 2 voicingDown
	{2, nullptr, &Track::repeatTail},                // 3 repeatTail
	{kNumGrooves, &Track::groove, nullptr},          // 4 groove
	{int(sizeof(kGrooveBars) / sizeof(kGrooveBars[0])), &Track::grooveLength, nullptr}, // 5 grooveLength
	{2, nullptr, &Track::grooveLoop},                // 6 grooveLoop
	{int(sizeof(kSwingSubdivisions) / sizeof(kSwingSubdivisions[0])), &Track::swingSubdivision, nullptr}, // 7
	{kNumPhrases, &Track::phrase, nullptr},          // 8 phrase
	{int(sizeof(kGrooveBars) / sizeof(kGrooveBars[0])), &Track::phraseLength, nullptr}, // 9 phraseLength
	{2, nullptr, &Track::phraseLoop},                // 10 phraseLoop
	{kNumScales, &Track::scale, nullptr},            // 11 scale
	{12, &Track::root, nullptr},                     // 12 root
	{2, nullptr, &Track::cc},                        // 13 cc
	{kNumCcNumbers, &Track::ccNumber, nullptr},      // 14 ccNumber
};
constexpr int kNumFields = int(sizeof(kFields) / sizeof(kFields[0]));

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE int talea_get_field(Host* h, int track, int id) {
	if (!validTrack(track) || id < 0 || id >= kNumFields)
		return 0;
	const Track& t = h->engine.pattern.tracks[track];
	const Field& f = kFields[id];
	return f.i ? t.*(f.i) : int(t.*(f.b));
}

/** Sets a track setting (clamped to its range). */
EMSCRIPTEN_KEEPALIVE void talea_set_field(Host* h, int track, int id, int v) {
	if (!validTrack(track) || id < 0 || id >= kNumFields)
		return;
	Track& t = h->engine.pattern.tracks[track];
	const Field& f = kFields[id];
	v = std::max(0, std::min(f.count - 1, v));
	if (f.i)
		t.*(f.i) = v;
	else
		t.*(f.b) = v != 0;
}

/** The sequencer's settings: 0 mute timing (MuteTiming), 1 mute cuts (0 finish, 1 cut),
2 pattern switch timing (MuteTiming). */
EMSCRIPTEN_KEEPALIVE int talea_get_setting(Host* h, int id) {
	const Engine& e = h->engine;
	return id == 0 ? e.muteTiming : id == 1 ? int(e.muteCuts) : id == 2 ? e.patternQuantize : 0;
}

EMSCRIPTEN_KEEPALIVE void talea_set_setting(Host* h, int id, int v) {
	Engine& e = h->engine;
	v = std::max(0, std::min(int(kNumMuteTimings) - 1, v));
	if (id == 0)
		e.muteTiming = v;
	else if (id == 1)
		e.muteCuts = v != 0;
	else if (id == 2)
		e.patternQuantize = v;
}

/** PATTERN page COPY TO: the playing pattern into another slot (onto itself: nothing). */
EMSCRIPTEN_KEEPALIVE void talea_copy_pattern(Host* h, int slot) {
	if (slot >= 0 && slot < kNumPatterns && slot != h->engine.currentPattern())
		h->engine.storePattern(slot, h->engine.committedPattern());
}

/** PATTERN page CLEAR: a slot back to the default (silent) pattern, the playing one too. */
EMSCRIPTEN_KEEPALIVE void talea_clear_pattern(Host* h, int slot) {
	if (slot >= 0 && slot < kNumPatterns)
		h->engine.clearPattern(slot);
}

} // extern "C"

// ---- the display's diagrams ------------------------------------------------------------

namespace {

/** The latest diagram's values (talea_diagram), read one by one. */
double gDiagram[160];

/** A diagram's x axis, as Rack's display draws it: musical time over `count` bars, or with
the page's sync on, the track's loops. */
struct Axis {
	bool loops;
	double span;
	double time(double x) const {
		return loops ? 0.0 : x;
	}
	double loopPos(double x) const {
		return loops ? x : kLoopsFromTime;
	}
};

Axis axisOf(const Track& tr, int kind) {
	if (kind == 0) {
		// A percussion groove is one bar (or, synced, one loop), repeated.
		int count = tr.groove < GROOVE_SAW ? 1 : kGrooveBars[tr.grooveLength];
		return tr.grooveLoop ? Axis{true, double(count)} : Axis{false, 4.0 * count};
	}
	int count = kGrooveBars[tr.phraseLength];
	return tr.phraseLoop ? Axis{true, double(count)} : Axis{false, 4.0 * count};
}

} // namespace

extern "C" {

/** Works out a diagram of the track and returns how many values it has (read them with
talea_diagram_value): 0 GROOVE, the accent shape over its span (129 values, -1..1); 1
PHRASE, the notes over the phrase (64 MIDI notes); 2 STYLE, the next 16 notes; 3 the User
phrase's 16 column levels (0..11). */
EMSCRIPTEN_KEEPALIVE int talea_diagram(Host* h, int track, int kind) {
	if (!validTrack(track))
		return 0;
	const Track& tr = h->engine.pattern.tracks[track];
	switch (kind) {
		case 0: {
			Axis a = axisOf(tr, 0);
			for (int i = 0; i <= 128; i++) {
				double t = std::min(a.span * i / 128, a.span - 1e-9);
				gDiagram[i] = tr.grooveValue(a.time(t), a.loopPos(t));
			}
			return 129;
		}
		case 1: {
			Axis a = axisOf(tr, 1);
			for (int i = 0; i < 64; i++) {
				double x = a.span * i / 64;
				gDiagram[i] = tr.noteFor(0, a.time(x), 0.0, 0, a.loopPos(x));
			}
			return 64;
		}
		case 2:
			for (int i = 0; i < 16; i++)
				gDiagram[i] = tr.noteFor(i, 0.0);
			return 16;
		case 3:
			for (int c = 0; c < kPhraseColumns; c++)
				gDiagram[c] = phraseLevel(tr.userPhrase, c);
			return kPhraseColumns;
		default: return 0;
	}
}

EMSCRIPTEN_KEEPALIVE double talea_diagram_value(int i) {
	return i >= 0 && i < int(sizeof(gDiagram) / sizeof(gDiagram[0])) ? gDiagram[i] : 0.0;
}

/** Where the playhead is across diagram `kind` (0 groove, else phrase) at host time `t`
(quarter notes): 0..1, or -1 before the start. */
EMSCRIPTEN_KEEPALIVE double talea_diagram_phase(Host* h, int track, int kind, double t) {
	if (!validTrack(track) || t <= 0.0)
		return -1.0;
	Axis a = axisOf(h->engine.pattern.tracks[track], kind == 0 ? 0 : 1);
	double now = a.loops ? h->engine.loopsAt(track, t) : t;
	double phase = now / a.span;
	return phase - std::floor(phase);
}

/** The User phrase: column `col` set to `level` (0..11), as a click on Rack's grid. */
EMSCRIPTEN_KEEPALIVE void talea_set_phrase_level(Host* h, int track, int col, int level) {
	if (validTrack(track) && col >= 0 && col < kPhraseColumns) {
		Track& tr = h->engine.pattern.tracks[track];
		tr.userPhrase = setPhraseLevel(tr.userPhrase, col, std::max(0, std::min(kPhraseLevels - 1, level)));
	}
}

} // extern "C"

// ---- the RANDOM page -------------------------------------------------------------------

extern "C" {

/** A track's random settings (TrackRandom): 0 parameter p's amount (-100..100 %), 1 whether
p moves per cycle loop (1) or per Random Rate tick (0), 2 Variance (0..100 %), 3 Random Rate
(index into kRandomRates), 4 phase, 5 Probability phase, 6 slew (0..100 %), 7 locked. */
EMSCRIPTEN_KEEPALIVE double talea_get_random(Host* h, int track, int what, int p) {
	if (!validTrack(track))
		return 0.0;
	const TrackRandom& r = h->engine.pattern.random[track];
	bool param = p >= 0 && p < kNumTrackParams;
	switch (what) {
		case 0: return param ? r.amount[p] : 0.0;
		case 1: return param && r.isPerCycle(TrackParam(p)) ? 1.0 : 0.0;
		case 2: return r.variance;
		case 3: return r.rate;
		case 4: return r.phase;
		case 5: return r.probPhase;
		case 6: return r.slew;
		case 7: return r.locked ? 1.0 : 0.0;
		default: return 0.0;
	}
}

/** Sets one of them (clamped). Amounts of parameters that can't be randomised stay 0. */
EMSCRIPTEN_KEEPALIVE void talea_set_random(Host* h, int track, int what, int p, double v) {
	if (!validTrack(track))
		return;
	TrackRandom& r = h->engine.pattern.random[track];
	bool param = p >= 0 && p < kNumTrackParams && randomisable(TrackParam(p));
	switch (what) {
		case 0:
			if (param)
				r.amount[p] = float(std::max(-100.0, std::min(100.0, std::round(v))));
			break;
		case 1:
			if (param)
				r.perCycle = v != 0.0 ? r.perCycle | (1u << p) : r.perCycle & ~(1u << p);
			break;
		case 2: r.variance = float(std::max(0.0, std::min(100.0, v))); break;
		case 3: r.rate = std::max(0, std::min(int(sizeof(kRandomRates) / sizeof(kRandomRates[0])) - 1, int(v))); break;
		case 6: r.slew = float(std::max(0.0, std::min(100.0, v))); break;
		default: break;
	}
}

/** The RANDOM page's buttons: 0 Regen (a new seed), 1 Lock or unlock (locking where the
sequences are at host time `now`), 2 phase -, 3 phase +, 4 Probability phase -, 5 +,
6 clear every amount. */
EMSCRIPTEN_KEEPALIVE void talea_random_action(Host* h, int track, int action, double now) {
	if (!validTrack(track))
		return;
	TrackRandom& r = h->engine.pattern.random[track];
	switch (action) {
		case 0: r.regenerate(); break;
		case 1:
			if (r.locked)
				r.locked = false;
			else
				h->engine.lockRandom(track, now);
			break;
		case 2: r.phase--; break;
		case 3: r.phase++; break;
		case 4: r.probPhase--; break;
		case 5: r.probPhase++; break;
		case 6:
			for (float& a : r.amount)
				a = 0.f;
			break;
		default: break;
	}
}

} // extern "C"

// ---- TRACK, KEYS and SCALE's notes -----------------------------------------------------

extern "C" {

/** The TRACK page's info line, as Rack's display writes it (without the clock). */
EMSCRIPTEN_KEEPALIVE const char* talea_track_info(Host* h, int track) {
	if (!validTrack(track))
		return "";
	const Track& tr = h->engine.pattern.tracks[track];
	int played = tr.playedSteps();
	std::string s = "TRACK " + std::to_string(track + 1);
	if (tr.cc)
		s += "  CC " + std::to_string(tr.ccNumber);
	s += "  STEPS " + std::to_string(tr.steps()) + "  PULSES " + std::to_string(tr.pulses()) + "  ROT " +
	     std::to_string(tr.getInt(TP_ROTATE)) + "  " + kDivisions[tr.getInt(TP_DIVISION)].name + "  LEN " +
	     kLengths[tr.getInt(TP_LENGTH)].name;
	if (played < tr.steps())
		s += "  (" + std::to_string(played) + "/" + std::to_string(tr.steps()) + " PLAY)";
	h->text = s;
	return h->text.c_str();
}

/** The track's note pool: its size, and note i (MIDI, ascending). */
EMSCRIPTEN_KEEPALIVE int talea_pool_size(Host* h, int track) {
	return validTrack(track) ? h->engine.pattern.tracks[track].poolSize : 0;
}

EMSCRIPTEN_KEEPALIVE int talea_pool_note(Host* h, int track, int i) {
	if (!validTrack(track))
		return 0;
	const Track& tr = h->engine.pattern.tracks[track];
	return i >= 0 && i < tr.poolSize ? tr.pool[i] : 0;
}

/** The track's scale as 12 bits by pitch class from C (the root applied). */
EMSCRIPTEN_KEEPALIVE int talea_scale_notes(Host* h, int track) {
	if (!validTrack(track))
		return 0;
	const Track& tr = h->engine.pattern.tracks[track];
	int bits = 0;
	for (int pc = 0; pc < 12; pc++)
		bits |= inScale(60 + pc, tr.scaleMask(), tr.root) ? 1 << pc : 0;
	return bits;
}

/** KEYS: a key click adds the note to the pool or takes it out, if it's in the scale or
already in the pool (as Rack's keys; the last note stays). */
EMSCRIPTEN_KEEPALIVE void talea_pool_key(Host* h, int track, int note) {
	if (!validTrack(track) || note < 0 || note > 127)
		return;
	Track& tr = h->engine.pattern.tracks[track];
	if (tr.inPool(note) || inScale(note, tr.scaleMask(), tr.root))
		tr.togglePoolNote(note);
}

/** SCALE's notes: a click toggles pitch class `pc` (0 = C) in the scale, which becomes the
User scale first (as Rack's). */
EMSCRIPTEN_KEEPALIVE void talea_scale_key(Host* h, int track, int pc) {
	if (!validTrack(track) || pc < 0 || pc > 11)
		return;
	Track& tr = h->engine.pattern.tracks[track];
	if (tr.scale != kUserScale) {
		tr.userScale = tr.scaleMask();
		tr.scale = kUserScale;
	}
	tr.userScale ^= ScaleMask(1u << (((pc - tr.root) % 12 + 12) % 12));
}

} // extern "C"

// ---- cycles ------------------------------------------------------------------------------
//
// A track's cycles (1..kMaxCycles) play in turn, a loop each; each can override the
// track's values and step edits, or be muted. `c` is a cycle slot (0..kMaxCycles-1).

extern "C" {

static bool validCycle(int c) {
	return c >= 0 && c < kMaxCycles;
}

EMSCRIPTEN_KEEPALIVE int talea_cycle_count(Host* h, int track) {
	return validTrack(track) ? h->engine.pattern.cycles[track].count : 1;
}

EMSCRIPTEN_KEEPALIVE void talea_set_cycle_count(Host* h, int track, int n) {
	if (validTrack(track))
		h->engine.pattern.cycles[track].count = std::max(1, std::min(kMaxCycles, n));
}

/** Cycle c: bit 0 muted, bit 1 plays differently from the track (overrides, step edits...),
bit 2 own step edits. */
EMSCRIPTEN_KEEPALIVE int talea_cycle_flags(Host* h, int track, int c) {
	if (!validTrack(track) || !validCycle(c))
		return 0;
	const CycleOverrides& o = h->engine.pattern.cycles[track].cycles[c];
	return (o.muted ? 1 : 0) | (o.modified() ? 2 : 0) | (o.ownHits ? 4 : 0);
}

EMSCRIPTEN_KEEPALIVE void talea_set_cycle_muted(Host* h, int track, int c, int muted) {
	if (validTrack(track) && validCycle(c))
		h->engine.pattern.cycles[track].cycles[c].muted = muted != 0;
}

/** The parameters cycle c overrides (a bit per parameter). */
EMSCRIPTEN_KEEPALIVE int talea_cycle_overrides(Host* h, int track, int c) {
	return validTrack(track) && validCycle(c) ? int(h->engine.pattern.cycles[track].cycles[c].overridden) : 0;
}

/** The value parameter p plays with in cycle c (its override, else the track's). */
EMSCRIPTEN_KEEPALIVE float talea_get_cycle_param(Host* h, int track, int c, int p) {
	if (!validTrack(track) || !validCycle(c) || p < 0 || p >= kNumTrackParams)
		return 0.f;
	return h->engine.pattern.cycles[track].value(h->engine.pattern.tracks[track], c, TrackParam(p));
}

/** Cycle edit: a dial overrides p in cycle c. */
EMSCRIPTEN_KEEPALIVE void talea_set_cycle_param(Host* h, int track, int c, int p, float v) {
	if (validTrack(track) && validCycle(c) && p >= 0 && p < kNumTrackParams)
		h->engine.pattern.cycles[track].set(c, TrackParam(p), v);
}

/** CYCLE page CLEAR: 0 all of cycle c's changes (its mute stays), 1 its step edits,
2 its override of p. */
EMSCRIPTEN_KEEPALIVE void talea_cycle_clear(Host* h, int track, int c, int what, int p) {
	if (!validTrack(track) || !validCycle(c))
		return;
	TrackCycles& cyc = h->engine.pattern.cycles[track];
	CycleOverrides& o = cyc.cycles[c];
	if (what == 0)
		o.clearChanges();
	else if (what == 1) {
		o.ownHits = false;
		o.manualHits = o.removedHits = 0;
	}
	else if (what == 2 && p >= 0 && p < kNumTrackParams)
		cyc.clear(c, TrackParam(p));
}

/** Cycle edit: a pad toggles played step `step` in cycle c (its first step edit starts from
the track's hits). */
EMSCRIPTEN_KEEPALIVE void talea_toggle_cycle_step(Host* h, int track, int c, int step) {
	if (validTrack(track) && validCycle(c) && step >= 0 && step < kMaxSteps)
		h->engine.pattern.cycles[track].toggleStep(h->engine.pattern.tracks[track], c, step);
}

/** Cycle c's hits as it plays them, in two 32-bit halves (as talea_hits_low/high). */
EMSCRIPTEN_KEEPALIVE double talea_cycle_hits_low(Host* h, int track, int c) {
	return validTrack(track) && validCycle(c) ? double(uint32_t(h->engine.hits(track, c) & 0xffffffffu)) : 0.0;
}

EMSCRIPTEN_KEEPALIVE double talea_cycle_hits_high(Host* h, int track, int c) {
	return validTrack(track) && validCycle(c) ? double(uint32_t(h->engine.hits(track, c) >> 32)) : 0.0;
}

/** Cycle c's Steps, and how many play before its Length cuts the loop. */
EMSCRIPTEN_KEEPALIVE int talea_cycle_steps(Host* h, int track, int c) {
	if (!validTrack(track) || !validCycle(c))
		return 0;
	return h->engine.pattern.cycles[track].resolve(h->engine.pattern.tracks[track], c).steps();
}

EMSCRIPTEN_KEEPALIVE int talea_cycle_played_steps(Host* h, int track, int c) {
	if (!validTrack(track) || !validCycle(c))
		return 0;
	return h->engine.pattern.cycles[track].resolve(h->engine.pattern.tracks[track], c).playedSteps();
}

/** Cycle edit: the edited cycle plays alone (c), or all in turn (-1). */
EMSCRIPTEN_KEEPALIVE void talea_set_solo_cycle(Host* h, int track, int c) {
	if (validTrack(track))
		h->engine.soloCycle[track] = validCycle(c) ? c : -1;
}

/** The cycle slot the track plays at time t (quarter notes since the song's start). */
EMSCRIPTEN_KEEPALIVE int talea_cycle_at(Host* h, int track, double t) {
	return validTrack(track) ? h->engine.cycleAt(track, t) : 0;
}

/** TRACK's info line for cycle c as it plays (Cycle edit), as talea_track_info. */
EMSCRIPTEN_KEEPALIVE const char* talea_cycle_info(Host* h, int track, int c) {
	if (!validTrack(track) || !validCycle(c))
		return "";
	Track saved = h->engine.pattern.tracks[track];
	h->engine.pattern.tracks[track] = h->engine.pattern.cycles[track].resolve(saved, c);
	talea_track_info(h, track);
	h->engine.pattern.tracks[track] = saved;
	h->text = "CYCLE " + std::to_string(c + 1) + "  " + h->text;
	return h->text.c_str();
}

} // extern "C"
