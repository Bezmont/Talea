#include "Test.hpp"
#include "core/Engine.hpp"
#include "core/State.hpp"

#include <vector>

using namespace talea;

namespace {

template <size_t N>
float indexOf(const NamedValue (&list)[N], const char* name) {
	for (size_t i = 0; i < N; i++) {
		if (std::string(list[i].name) == name)
			return float(i);
	}
	return -1.f;
}

int scaleIndex(const char* name) {
	for (int i = 0; i < kNumScales; i++) {
		if (std::string(kScales[i].name) == name)
			return i;
	}
	return -1;
}

std::vector<int> notesPlayed(Engine& e, double to, int track = 0) {
	std::vector<int> out;
	EventList list;
	for (double t = -1e-9; t < to - 1e-12; t += 0.05) {
		list.clear();
		e.process(t, std::min(t + 0.05, to), list);
		for (const NoteEvent& n : list) {
			if (n.on && n.track == track)
				out.push_back(n.note);
		}
	}
	return out;
}

void setPool(Track& t, std::initializer_list<int> notes) {
	t.poolSize = 0;
	for (int n : notes)
		t.pool[t.poolSize++] = n;
}

} // namespace

TEST_CASE(scale_quantize_and_neighbours) {
	ScaleMask major = kScales[scaleIndex("Major (Ionian)")].mask;
	CHECK_EQ(quantize(61, major, 0), 60); // C#: tie goes down
	CHECK_EQ(quantize(63, major, 0), 62); // D#: tie goes down
	CHECK_EQ(quantize(66, major, 0), 65);
	CHECK_EQ(quantize(61, major, 1), 61); // C# major: C# is the root
	CHECK_EQ(quantize(70, 0, 0), 70);     // empty user scale: unchanged
	CHECK_EQ(scaleNeighbour(60, major, 0, 1), 62);
	CHECK_EQ(scaleNeighbour(60, major, 0, -1), 59);
	CHECK_EQ(scaleNeighbour(64, major, 0, 1), 65);
	CHECK(inScale(72, major, 0) && !inScale(73, major, 0));
}

TEST_CASE(style_positions) {
	auto seq = [](int style, int amount, int n) {
		std::vector<int> out;
		for (int i = 0; i < n; i++)
			out.push_back(stylePosition(style, i, amount));
		return out;
	};
	CHECK(seq(STYLE_FIXED, 3, 3) == std::vector<int>({3, 3, 3}));
	CHECK(seq(STYLE_RAMP, 2, 5) == std::vector<int>({0, 1, 2, 0, 1}));
	CHECK(seq(STYLE_CLIMB, 4, 9) == std::vector<int>({0, 1, 2, 1, 2, 3, 2, 3, 4}));
	CHECK(seq(STYLE_UP_DOWN, 2, 6) == std::vector<int>({0, 1, 2, 1, 0, 1}));
	CHECK(seq(STYLE_CLIMB_UP_DOWN, 2, 6) == std::vector<int>({0, 1, 2, 1, 2, 1}));
	CHECK(seq(STYLE_RAMP, 0, 3) == std::vector<int>({0, 0, 0}));
}

TEST_CASE(voicing_moves_by_octaves) {
	int up[] = {60, 64, 67};
	voice(up, 3, 1, true);
	CHECK(up[0] == 64 && up[1] == 67 && up[2] == 72);
	int down[] = {60, 64, 67};
	voice(down, 3, 4, false);
	CHECK(down[0] == 43 && down[1] == 48 && down[2] == 52);
}

TEST_CASE(pool_editing_and_harmony) {
	Track t;
	CHECK(t.poolSize == 1 && t.pool[0] == 60);
	CHECK(!t.togglePoolNote(60)); // the last note stays
	CHECK(t.togglePoolNote(67));
	CHECK(t.togglePoolNote(64));
	CHECK(t.poolSize == 3 && t.pool[0] == 60 && t.pool[1] == 64 && t.pool[2] == 67);
	for (int n = 70; n < 75; n++)
		t.togglePoolNote(n);
	CHECK_EQ(t.poolSize, kMaxPool);
	CHECK(!t.togglePoolNote(80));

	Track h;
	setPool(h, {60, 64, 67});
	h.scale = scaleIndex("Major (Ionian)");
	h.harmony(1); // lowest up a scale step
	CHECK(h.pool[0] == 62 && h.pool[1] == 64 && h.pool[2] == 67);
	h.harmony(-1); // highest down a scale step
	CHECK(h.pool[0] == 62 && h.pool[1] == 64 && h.pool[2] == 65);
	h.harmony(1); // 62 -> 64 is taken, so it lands on 65... also taken: 67
	CHECK(h.pool[0] == 64 && h.pool[1] == 65 && h.pool[2] == 67);
}

TEST_CASE(note_pipeline) {
	Track t;
	setPool(t, {60, 64, 67});
	CHECK_EQ(t.noteFor(0, 0.0), 60); // Poly: lowest chord note on the main output
	t.set(TP_PITCH, 7);
	CHECK_EQ(t.noteFor(0, 0.0), 67);
	t.scale = scaleIndex("Major (Ionian)");
	t.set(TP_PITCH, 1);
	CHECK_EQ(t.noteFor(0, 0.0), 60); // C# snapped back into C major
	t.set(TP_PITCH, 0);
	t.set(TP_VOICING, 1); // Fixed: one move up -> E G C
	CHECK_EQ(t.noteFor(0, 0.0), 64);
	t.set(TP_VOICING, 0);

	t.set(TP_RANGE, 12);
	t.phrase = PHRASE_SAW;
	t.phraseLength = 0;                 // 1 bar
	CHECK_EQ(t.noteFor(0, 2.0), 65);   // halfway: +6 = F#, quantized down to F
	t.phrase = PHRASE_CV;
	CHECK_EQ(t.noteFor(0, 0.0, 5.0), 72); // CV at +5 V = full Range
	CHECK_EQ(t.noteFor(0, 0.0, -5.0, 2), 62); // transpose 2 semitones
}

TEST_CASE(engine_arpeggiates_mono_across_pulses) {
	Engine e;
	Track& t = e.pattern.tracks[0];
	t.set(TP_PULSES, 16);
	t.set(TP_LENGTH, indexOf(kLengths, "1/4")); // 4 steps per loop: the motion restarts
	setPool(t, {60, 64, 67});
	t.mono = true;
	CHECK(notesPlayed(e, 0.5 - 1e-6) == std::vector<int>({60, 64}));
	CHECK(notesPlayed(e, 1.25 - 1e-6) == std::vector<int>({60, 64, 67, 60, 60})); // replay from the start
}

TEST_CASE(engine_style_with_repeats) {
	// Tail: pulses play the starting position, each burst's repeats run the motion.
	Engine tail;
	Track& t = tail.pattern.tracks[0];
	t.set(TP_PULSES, 1);
	t.set(TP_REPEATS, 2);
	setPool(t, {60, 64, 67});
	t.mono = true;
	t.repeatTail = true;
	CHECK(notesPlayed(tail, 1.0) == std::vector<int>({60, 64, 67}));

	// Choke: the motion runs through pulses and repeats alike.
	Engine choke;
	Track& c = choke.pattern.tracks[0];
	c.set(TP_PULSES, 2);
	c.set(TP_REPEATS, 1);
	setPool(c, {60, 64, 67});
	c.mono = true;
	CHECK(notesPlayed(choke, 4.0 - 1e-6) == std::vector<int>({60, 64, 67, 60}));
}

TEST_CASE(engine_host_transpose) {
	Engine e;
	e.pattern.tracks[0].set(TP_PULSES, 1);
	e.pattern.tracks[0].scale = scaleIndex("Major (Ionian)");
	e.transpose[0] = 1; // C# -> C in C major
	CHECK(notesPlayed(e, 0.5) == std::vector<int>({60}));
	Engine f;
	f.pattern.tracks[0].set(TP_PULSES, 1);
	f.transpose[0] = -12;
	CHECK(notesPlayed(f, 0.5) == std::vector<int>({48}));
}

TEST_CASE(tonal_state_round_trip) {
	Pattern a;
	Track& t = a.tracks[4];
	setPool(t, {48, 55, 63, 70});
	t.scale = scaleIndex("Dorian");
	t.root = 2;
	t.userScale = maskOf({0, 3, 7});
	t.style = STYLE_CLIMB;
	t.mono = true;
	t.voicingDown = true;
	t.phrase = PHRASE_SINE;
	t.phraseLength = 4;
	Pattern b;
	CHECK(patternFromJson(patternToJson(a), b));
	const Track& r = b.tracks[4];
	CHECK_EQ(r.poolSize, 4);
	CHECK(r.pool[0] == 48 && r.pool[3] == 70);
	CHECK_EQ(r.scale, t.scale);
	CHECK_EQ(r.root, 2);
	CHECK_EQ(r.userScale, t.userScale);
	CHECK_EQ(r.style, int(STYLE_CLIMB));
	CHECK(r.mono && r.voicingDown);
	CHECK_EQ(r.phrase, int(PHRASE_SINE));
	CHECK_EQ(r.phraseLength, 4);
	CHECK(b.tracks[0].poolSize == 1 && b.tracks[0].pool[0] == 60);
}

TEST_CASE(chord_events_carry_the_whole_chord) {
	Engine e;
	Track& t = e.pattern.tracks[0];
	t.set(TP_PULSES, 4);
	t.togglePoolNote(64);
	t.togglePoolNote(67);
	EventList list;
	e.process(-1e-9, 0.1, list);
	const NoteEvent* on = nullptr;
	for (const NoteEvent& n : list) {
		if (n.on && n.track == 0)
			on = &n;
	}
	CHECK(on != nullptr);
	if (on) {
		CHECK_EQ(on->chordSize, 3);
		CHECK_EQ(int(on->chord[0]), 60);
		CHECK_EQ(int(on->chord[1]), 64);
		CHECK_EQ(int(on->chord[2]), 67);
		CHECK_EQ(on->note, 60);
	}
	int out[kMaxPool];
	t.mono = true;
	CHECK_EQ(t.chordFor(1, 0.0, out), 1);
	CHECK_EQ(out[0], 64);
	CHECK_EQ(out[0], t.noteFor(1, 0.0));
}
