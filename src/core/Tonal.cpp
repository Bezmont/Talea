#include "core/Tonal.hpp"

#include <algorithm>
#include <cmath>

namespace talea {

bool inScale(int note, ScaleMask mask, int root) {
	int pc = ((note - root) % 12 + 12) % 12;
	return mask & (1u << pc);
}

int quantize(int note, ScaleMask mask, int root) {
	if ((mask & 0xFFF) == 0)
		return note;
	for (int d = 0; d <= 6; d++) {
		if (inScale(note - d, mask, root))
			return note - d;
		if (inScale(note + d, mask, root))
			return note + d;
	}
	return note;
}

int scaleNeighbour(int note, ScaleMask mask, int root, int dir) {
	int step = dir > 0 ? 1 : -1;
	if ((mask & 0xFFF) == 0)
		return note + step;
	for (int d = 1; d <= 12; d++) {
		if (inScale(note + step * d, mask, root))
			return note + step * d;
	}
	return note + step;
}

namespace {

/** 0, 1, 2, 1, 2, 3, 2, 3, 4...: two steps forward, one back. */
long climb(long s) {
	return s / 3 + s % 3;
}

/** 0..a..1 repeating. */
long pingPong(long s, int a) {
	if (a <= 0)
		return 0;
	long p = s % (2L * a);
	return p <= a ? p : 2L * a - p;
}

} // namespace

int stylePosition(int style, long ordinal, int amount) {
	if (amount <= 0 || ordinal < 0)
		return 0;
	switch (style) {
		case STYLE_RAMP: return int(ordinal % (amount + 1));
		case STYLE_CLIMB: return int(climb(ordinal) % (amount + 1));
		case STYLE_UP_DOWN: return int(pingPong(ordinal, amount));
		case STYLE_CLIMB_UP_DOWN: return int(pingPong(climb(ordinal), amount));
		default: return amount; // Fixed: the voicing as set
	}
}

void voice(int* notes, int count, int position, bool up) {
	if (count <= 0)
		return;
	for (int i = 0; i < position; i++) {
		if (up)
			notes[0] += 12;
		else
			notes[count - 1] -= 12;
		std::sort(notes, notes + count);
	}
}

namespace {

// Original contours: eight equal segments over the phrase, 0 = the pool, 1 = full Range.
constexpr float kCadences[4][8] = {
	{0, .5f, .25f, .75f, .5f, 1, .75f, 0},     // rise in waves, fall home
	{1, .75f, .5f, .75f, .25f, .5f, 0, 0},     // descend with turns
	{0, 0, .5f, .5f, .25f, .25f, .75f, 1},     // held steps up to the top
	{.5f, 1, .5f, 0, .5f, .75f, .25f, 0},      // question and answer
};

constexpr double kPi = 3.14159265358979323846;

} // namespace

int phraseLevel(PhraseGrid g, int col) {
	if (col < 0 || col >= kPhraseColumns)
		return 0;
	return std::clamp(int((g >> (4 * col)) & 0xF), 0, kPhraseLevels - 1);
}

PhraseGrid setPhraseLevel(PhraseGrid g, int col, int level) {
	if (col < 0 || col >= kPhraseColumns)
		return g;
	PhraseGrid v = PhraseGrid(std::clamp(level, 0, kPhraseLevels - 1));
	return (g & ~(PhraseGrid(0xF) << (4 * col))) | (v << (4 * col));
}

double phraseValue(int phrase, double phase, double cv, PhraseGrid user) {
	phase = phase - std::floor(phase);
	switch (phrase) {
		case PHRASE_CADENCE_1:
		case PHRASE_CADENCE_2:
		case PHRASE_CADENCE_3:
		case PHRASE_CADENCE_4: return kCadences[phrase][std::clamp(int(phase * 8.0), 0, 7)];
		case PHRASE_SAW: return phase;
		case PHRASE_TRIANGLE: return phase < 0.5 ? 2.0 * phase : 2.0 - 2.0 * phase;
		case PHRASE_SINE: return 0.5 - 0.5 * std::cos(2.0 * kPi * phase); // starts at the pool
		case PHRASE_PULSE: return phase < 0.5 ? 0.0 : 1.0;
		case PHRASE_USER: {
			int col = std::clamp(int(phase * kPhraseColumns), 0, kPhraseColumns - 1);
			return phraseLevel(user, col) / double(kPhraseLevels - 1);
		}
		case PHRASE_CV: return std::clamp((cv + 5.0) / 10.0, 0.0, 1.0);
		default: return 0.0;
	}
}

} // namespace talea
