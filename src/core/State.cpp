#include "core/State.hpp"

#include "core/third_party/nlohmann/json.hpp"

#include <algorithm>
#include <vector>

namespace talea {

using nlohmann::json;

namespace {

/** A step pool lock {"step": s, "pool": [notes]}; false without one. */
bool readPool(const json& j, int& step, int* notes, int& n) {
	if (!j.is_object())
		return false;
	auto s = j.find("step");
	auto pool = j.find("pool");
	if (s == j.end() || !s->is_number_integer() || pool == j.end() || !pool->is_array())
		return false;
	step = s->get<int>();
	n = 0;
	for (const json& v : *pool) {
		if (v.is_number_integer() && n < kMaxPool)
			notes[n++] = v.get<int>();
	}
	return n > 0;
}

} // namespace

std::string patternToJson(const Pattern& pattern) {
	json tracks = json::array();
	for (int i = 0; i < kNumTracks; i++) {
		const Track& t = pattern.tracks[i];
		json jt = json::object();
		for (int p = 0; p < kNumTrackParams; p++)
			jt[kTrackParams[p].key] = t.values[p];
		jt["manualHits"] = t.manualHits;
		jt["removedHits"] = t.removedHits;
		jt["repeatTail"] = t.repeatTail;
		jt["cc"] = t.cc;
		jt["ccNumber"] = t.ccNumber;
		jt["groove"] = t.groove;
		jt["grooveLength"] = t.grooveLength;
		jt["swingSubdivision"] = t.swingSubdivision;
		jt["pool"] = std::vector<int>(t.pool, t.pool + t.poolSize);
		jt["scale"] = t.scale;
		jt["root"] = t.root;
		jt["userScale"] = t.userScale;
		jt["style"] = t.style;
		jt["mono"] = t.mono;
		jt["voicingDown"] = t.voicingDown;
		jt["phrase"] = t.phrase;
		jt["phraseLength"] = t.phraseLength;
		jt["muted"] = t.muted;
		// Cycles: the count, and only the slots that differ from the track.
		const TrackCycles& cyc = pattern.cycles[i];
		json slots = json::array();
		for (int c = 0; c < kMaxCycles; c++) {
			const CycleOverrides& o = cyc.cycles[c];
			if (o.empty())
				continue;
			json values = json::object();
			for (int p = 0; p < kNumTrackParams; p++) {
				if (o.has(TrackParam(p)))
					values[kTrackParams[p].key] = o.values[p];
			}
			json slot = {{"slot", c}, {"muted", o.muted}, {"values", values}};
			if (o.randomOverridden) {
				json amounts = json::object();
				for (int p = 0; p < kNumTrackParams; p++) {
					if (o.hasRandom(TrackParam(p)))
						amounts[kTrackParams[p].key] = o.randomAmounts[p];
				}
				slot["random"] = amounts;
			}
			if (o.ownHits) {
				slot["manualHits"] = o.manualHits;
				slot["removedHits"] = o.removedHits;
			}
			if (o.ownTonal) {
				const Track& tn = o.tonal;
				slot["tonal"] = {{"scale", tn.scale}, {"root", tn.root}, {"userScale", tn.userScale},
				                 {"pool", std::vector<int>(tn.pool, tn.pool + tn.poolSize)}};
			}
			if (o.locks.count) {
				json locks = json::array();
				for (int i = 0; i < o.locks.count; i++)
					locks.push_back({{"step", o.locks.step[i]}, {"param", kTrackParams[o.locks.param[i]].key}, {"value", o.locks.value[i]}});
				slot["locks"] = locks;
			}
			if (o.locks.poolCount) {
				json pools = json::array();
				for (int k = 0; k < o.locks.poolCount; k++) {
					const PoolLock& pl = o.locks.pool[k];
					pools.push_back({{"step", o.locks.poolStep[k]}, {"pool", std::vector<int>(pl.notes, pl.notes + pl.size)}});
				}
				slot["poolLocks"] = pools;
			}
			slots.push_back(slot);
		}
		jt["cycles"] = {{"count", cyc.count}, {"slots", slots}};
		// Random: only the parameters with an amount.
		const TrackRandom& rnd = pattern.random[i];
		json amounts = json::object();
		for (int p = 0; p < kNumTrackParams; p++) {
			if (rnd.amount[p] != 0.f)
				amounts[kTrackParams[p].key] = rnd.amount[p];
		}
		jt["random"] = {{"amounts", amounts}, {"perCycle", rnd.perCycle}, {"variance", rnd.variance},
		                {"rate", rnd.rate}, {"seed", rnd.seed}, {"phase", rnd.phase}, {"probPhase", rnd.probPhase},
		                {"slew", rnd.slew}, {"locked", rnd.locked}, {"lockLoop", rnd.lockLoop}, {"lockTick", rnd.lockTick}};
		// Step locks: only the steps with any.
		const StepLocks& locks = pattern.locks[i];
		json jl = json::array();
		for (int s = 0; s < kMaxSteps; s++) {
			if (!locks.any(s))
				continue;
			json values = json::object();
			for (int p = 0; p < kNumTrackParams; p++) {
				if (locks.has(s, TrackParam(p)))
					values[kTrackParams[p].key] = locks.values[s][p];
			}
			json js = {{"step", s}, {"values", values}};
			if (const PoolLock* pl = locks.poolAt(s))
				js["pool"] = std::vector<int>(pl->notes, pl->notes + pl->size);
			jl.push_back(js);
		}
		jt["locks"] = jl;
		tracks.push_back(jt);
	}
	json root = {{"version", kStateVersion}, {"tracks", tracks}};
	return root.dump();
}

std::string songToJson(const Song& song) {
	json entries = json::array();
	for (int i = 0; i < song.count; i++)
		entries.push_back({{"pattern", song.entries[i].slot}, {"bars", song.entries[i].bars}});
	json root = {{"version", kStateVersion}, {"loop", song.loop}, {"entries", entries}};
	return root.dump();
}

bool songFromJson(const std::string& text, Song& song) {
	song = Song();
	json root = json::parse(text, nullptr, false);
	if (root.is_discarded() || !root.is_object())
		return false;
	auto loop = root.find("loop");
	if (loop != root.end() && loop->is_boolean())
		song.loop = loop->get<bool>();
	auto entries = root.find("entries");
	if (entries == root.end() || !entries->is_array())
		return true;
	for (const json& e : *entries) {
		if (!e.is_object())
			continue;
		auto slot = e.find("pattern");
		auto bars = e.find("bars");
		if (slot == e.end() || !slot->is_number_integer() || bars == e.end() || !bars->is_number_integer())
			continue;
		int s = slot->get<int>(), b = bars->get<int>();
		if (s < 0 || s >= kNumPatterns || b < 1)
			continue;
		song.insert(song.count, {s, std::min(b, kMaxSongBars)});
	}
	return true;
}

bool patternFromJson(const std::string& text, Pattern& pattern) {
	pattern.reset();
	json root = json::parse(text, nullptr, false);
	if (root.is_discarded() || !root.is_object())
		return false;
	auto tracks = root.find("tracks");
	if (tracks == root.end() || !tracks->is_array())
		return false;
	for (int i = 0; i < kNumTracks && i < int(tracks->size()); i++) {
		const json& jt = (*tracks)[i];
		if (!jt.is_object())
			continue;
		Track& t = pattern.tracks[i];
		for (int p = 0; p < kNumTrackParams; p++) {
			auto it = jt.find(kTrackParams[p].key);
			if (it != jt.end() && it->is_number())
				t.set(TrackParam(p), it->get<float>());
		}
		auto manual = jt.find("manualHits");
		if (manual != jt.end() && manual->is_number_unsigned())
			t.manualHits = manual->get<StepMask>();
		auto removed = jt.find("removedHits");
		if (removed != jt.end() && removed->is_number_unsigned())
			t.removedHits = removed->get<StepMask>();
		auto tail = jt.find("repeatTail");
		if (tail != jt.end() && tail->is_boolean())
			t.repeatTail = tail->get<bool>();
		auto readIndex = [&](const char* key, int& field, int count) {
			auto it = jt.find(key);
			if (it != jt.end() && it->is_number_integer())
				field = std::clamp(it->get<int>(), 0, count - 1);
		};
		readIndex("groove", t.groove, kNumGrooves);
		readIndex("grooveLength", t.grooveLength, countOf(kGrooveBars));
		readIndex("swingSubdivision", t.swingSubdivision, countOf(kSwingSubdivisions));
		readIndex("scale", t.scale, kNumScales);
		readIndex("root", t.root, 12);
		readIndex("style", t.style, kNumStyles);
		readIndex("phrase", t.phrase, kNumPhrases);
		readIndex("phraseLength", t.phraseLength, countOf(kGrooveBars));
		readIndex("ccNumber", t.ccNumber, kNumCcNumbers);
		auto cc = jt.find("cc");
		if (cc != jt.end() && cc->is_boolean())
			t.cc = cc->get<bool>();
		auto readBool = [&](const char* key, bool& field) {
			auto it = jt.find(key);
			if (it != jt.end() && it->is_boolean())
				field = it->get<bool>();
		};
		readBool("mono", t.mono);
		readBool("voicingDown", t.voicingDown);
		readBool("muted", t.muted);
		auto cycles = jt.find("cycles");
		if (cycles != jt.end() && cycles->is_object()) {
			TrackCycles& cyc = pattern.cycles[i];
			auto count = cycles->find("count");
			if (count != cycles->end() && count->is_number_integer())
				cyc.count = std::clamp(count->get<int>(), 1, kMaxCycles);
			auto slots = cycles->find("slots");
			if (slots != cycles->end() && slots->is_array()) {
				for (const json& slot : *slots) {
					auto c = slot.find("slot");
					if (!slot.is_object() || c == slot.end() || !c->is_number_integer())
						continue;
					int index = c->get<int>();
					if (index < 0 || index >= kMaxCycles)
						continue;
					auto muted = slot.find("muted");
					if (muted != slot.end() && muted->is_boolean())
						cyc.cycles[index].muted = muted->get<bool>();
					auto manual = slot.find("manualHits");
					auto removed = slot.find("removedHits");
					if (manual != slot.end() && manual->is_number_unsigned() && removed != slot.end()
						&& removed->is_number_unsigned()) {
						cyc.cycles[index].ownHits = true;
						cyc.cycles[index].manualHits = manual->get<StepMask>();
						cyc.cycles[index].removedHits = removed->get<StepMask>();
					}
					auto tonal = slot.find("tonal");
					if (tonal != slot.end() && tonal->is_object()) {
						Track& tn = cyc.tonalEdit(t, index);
						auto get = [&](const char* key, int& field, int count) {
							auto it = tonal->find(key);
							if (it != tonal->end() && it->is_number_integer())
								field = std::clamp(it->get<int>(), 0, count - 1);
						};
						get("scale", tn.scale, kNumScales);
						get("root", tn.root, 12);
						auto us = tonal->find("userScale");
						if (us != tonal->end() && us->is_number_unsigned())
							tn.userScale = ScaleMask(us->get<unsigned>() & 0xFFF);
						auto pool = tonal->find("pool");
						if (pool != tonal->end() && pool->is_array()) {
							int n = 0;
							for (const json& note : *pool) {
								if (n < kMaxPool && note.is_number_integer())
									tn.pool[n++] = std::clamp(note.get<int>(), 0, 127);
							}
							if (n > 0) {
								std::sort(tn.pool, tn.pool + n);
								tn.poolSize = int(std::unique(tn.pool, tn.pool + n) - tn.pool);
							}
						}
					}
					auto locks = slot.find("locks");
					if (locks != slot.end() && locks->is_array()) {
						for (const json& l : *locks) {
							auto st = l.find("step");
							auto key = l.find("param");
							auto v = l.find("value");
							if (!l.is_object() || st == l.end() || !st->is_number_integer() || key == l.end()
								|| !key->is_string() || v == l.end() || !v->is_number())
								continue;
							for (int p = 0; p < kNumTrackParams; p++) {
								if (key->get<std::string>() == kTrackParams[p].key)
									cyc.cycles[index].locks.set(st->get<int>(), TrackParam(p), v->get<float>());
							}
						}
					}
					auto pools = slot.find("poolLocks");
					if (pools != slot.end() && pools->is_array()) {
						for (const json& l : *pools) {
							int notes[kMaxPool];
							int n = 0, st = -1;
							if (readPool(l, st, notes, n))
								cyc.cycles[index].locks.setPool(st, notes, n);
						}
					}
					auto amounts = slot.find("random");
					if (amounts != slot.end() && amounts->is_object()) {
						for (int p = 0; p < kNumTrackParams; p++) {
							auto v = amounts->find(kTrackParams[p].key);
							if (v != amounts->end() && v->is_number())
								cyc.setRandom(index, TrackParam(p), v->get<float>());
						}
					}
					auto values = slot.find("values");
					if (values == slot.end() || !values->is_object())
						continue;
					for (int p = 0; p < kNumTrackParams; p++) {
						auto v = values->find(kTrackParams[p].key);
						if (v != values->end() && v->is_number())
							cyc.set(index, TrackParam(p), v->get<float>());
					}
				}
			}
		}
		auto random = jt.find("random");
		if (random != jt.end() && random->is_object()) {
			TrackRandom& rnd = pattern.random[i];
			auto amounts = random->find("amounts");
			if (amounts != random->end() && amounts->is_object()) {
				for (int p = 0; p < kNumTrackParams; p++) {
					auto v = amounts->find(kTrackParams[p].key);
					if (v != amounts->end() && v->is_number() && randomisable(TrackParam(p)))
						rnd.amount[p] = std::clamp(v->get<float>(), -100.f, 100.f);
				}
			}
			auto perCycle = random->find("perCycle");
			if (perCycle != random->end() && perCycle->is_number_unsigned())
				rnd.perCycle = perCycle->get<uint32_t>();
			auto variance = random->find("variance");
			if (variance != random->end() && variance->is_number())
				rnd.variance = std::clamp(variance->get<float>(), 0.f, 100.f);
			auto rate = random->find("rate");
			if (rate != random->end() && rate->is_number_integer())
				rnd.rate = std::clamp(rate->get<int>(), 0, countOf(kRandomRates) - 1);
			auto seed = random->find("seed");
			if (seed != random->end() && seed->is_number_unsigned())
				rnd.seed = seed->get<uint32_t>();
			auto readInt = [&](const char* key, auto& field) {
				auto it = random->find(key);
				if (it != random->end() && it->is_number_integer())
					field = it->get<std::remove_reference_t<decltype(field)>>();
			};
			readInt("phase", rnd.phase);
			readInt("probPhase", rnd.probPhase);
			readInt("lockLoop", rnd.lockLoop);
			readInt("lockTick", rnd.lockTick);
			auto slew = random->find("slew");
			if (slew != random->end() && slew->is_number())
				rnd.slew = std::clamp(slew->get<float>(), 0.f, 100.f);
			auto locked = random->find("locked");
			if (locked != random->end() && locked->is_boolean())
				rnd.locked = locked->get<bool>();
		}
		auto locks = jt.find("locks");
		if (locks != jt.end() && locks->is_array()) {
			for (const json& step : *locks) {
				if (!step.is_object())
					continue;
				auto s = step.find("step");
				auto values = step.find("values");
				int notes[kMaxPool];
				int n = 0, st = -1;
				if (readPool(step, st, notes, n))
					pattern.locks[i].setPool(st, notes, n);
				if (s == step.end() || !s->is_number_integer() || values == step.end() || !values->is_object())
					continue;
				for (int p = 0; p < kNumTrackParams; p++) {
					auto v = values->find(kTrackParams[p].key);
					if (v != values->end() && v->is_number())
						pattern.locks[i].set(s->get<int>(), TrackParam(p), v->get<float>()); // range-checked
				}
			}
		}
		auto user = jt.find("userScale");
		if (user != jt.end() && user->is_number_unsigned())
			t.userScale = ScaleMask(user->get<unsigned>() & 0xFFF);
		auto pool = jt.find("pool");
		if (pool != jt.end() && pool->is_array()) {
			int n = 0;
			for (const json& note : *pool) {
				if (n < kMaxPool && note.is_number_integer())
					t.pool[n++] = std::clamp(note.get<int>(), 0, 127);
			}
			if (n > 0) {
				t.poolSize = n;
				std::sort(t.pool, t.pool + n);
				t.poolSize = int(std::unique(t.pool, t.pool + n) - t.pool);
			}
		}
	}
	return true;
}

} // namespace talea
