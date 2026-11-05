#pragma once
#include <cstdint>
#include <initializer_list>

// Scales, quantizing, voicing motion and phrase shapes: the pitch side of the core, kept as
// data and pure functions so every host gets identical notes.

namespace talea {

/** 12-bit pitch-class set relative to the root: bit i = i semitones above the root. */
using ScaleMask = uint16_t;

constexpr ScaleMask maskOf(std::initializer_list<int> semitones) {
	ScaleMask m = 0;
	for (int s : semitones)
		m |= ScaleMask(1u << s);
	return m;
}

struct ScaleInfo {
	const char* name;
	const char* shortName; // fits a display button
	ScaleMask mask;
};

inline constexpr ScaleInfo kScales[] = {
	{"Chromatic", "CHROM", 0xFFF},
	{"Major (Ionian)", "MAJOR", maskOf({0, 2, 4, 5, 7, 9, 11})},
	{"Natural Minor (Aeolian)", "MINOR", maskOf({0, 2, 3, 5, 7, 8, 10})},
	{"Harmonic Minor", "HARM M", maskOf({0, 2, 3, 5, 7, 8, 11})},
	{"Melodic Minor", "MEL M", maskOf({0, 2, 3, 5, 7, 9, 11})},
	{"Dorian", "DORIAN", maskOf({0, 2, 3, 5, 7, 9, 10})},
	{"Phrygian", "PHRYG", maskOf({0, 1, 3, 5, 7, 8, 10})},
	{"Lydian", "LYDIAN", maskOf({0, 2, 4, 6, 7, 9, 11})},
	{"Mixolydian", "MIXO", maskOf({0, 2, 4, 5, 7, 9, 10})},
	{"Locrian", "LOCRIA", maskOf({0, 1, 3, 5, 6, 8, 10})},
	{"Major Pentatonic", "PENT M", maskOf({0, 2, 4, 7, 9})},
	{"Minor Pentatonic", "PENT m", maskOf({0, 3, 5, 7, 10})},
	{"Blues", "BLUES", maskOf({0, 3, 5, 6, 7, 10})},
	{"Major Blues", "BLUES M", maskOf({0, 2, 3, 4, 7, 9})},
	{"Whole Tone", "WHOLE", maskOf({0, 2, 4, 6, 8, 10})},
	{"Diminished (H-W)", "DIM HW", maskOf({0, 1, 3, 4, 6, 7, 9, 10})},
	{"Diminished (W-H)", "DIM WH", maskOf({0, 2, 3, 5, 6, 8, 9, 11})},
	{"Phrygian Dominant", "PHR DOM", maskOf({0, 1, 4, 5, 7, 8, 10})},
	{"Hungarian Minor", "HUNG M", maskOf({0, 2, 3, 6, 7, 8, 11})},
	{"Double Harmonic", "DBL HAR", maskOf({0, 1, 4, 5, 7, 8, 11})},
	{"Japanese (In)", "IN", maskOf({0, 1, 5, 7, 8})},
	{"Hirajoshi", "HIRAJO", maskOf({0, 2, 3, 7, 8})},
	{"Arabic", "ARABIC", maskOf({0, 2, 4, 5, 6, 8, 10})},
	{"User", "USER", 0}, // the track's own mask
};
inline constexpr int kNumScales = int(sizeof(kScales) / sizeof(kScales[0]));
inline constexpr int kUserScale = kNumScales - 1;
inline constexpr ScaleMask kDefaultUserScale = maskOf({0, 2, 4, 5, 7, 9, 11});

inline constexpr const char* kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

/** Whether MIDI `note` is in the scale `mask` on `root` (0 = C). */
bool inScale(int note, ScaleMask mask, int root);
/** Nearest scale note; ties go down. An empty mask leaves the note alone. */
int quantize(int note, ScaleMask mask, int root);
/** The next scale note strictly above (dir > 0) or below (dir < 0) `note`. */
int scaleNeighbour(int note, ScaleMask mask, int root, int dir);

/** Voicing motion over successive notes. */
enum Style { STYLE_FIXED, STYLE_RAMP, STYLE_CLIMB, STYLE_UP_DOWN, STYLE_CLIMB_UP_DOWN, kNumStyles };
inline constexpr const char* kStyleNames[kNumStyles] = {"Fixed", "Ramp", "Climb", "Up/Down", "Climb Up/Down"};

/** Voicing position (0..amount) of the `ordinal`-th note under `style`. */
int stylePosition(int style, long ordinal, int amount);

/** Applies `position` voicing moves to the sorted chord in place: each move takes the lowest
note up an octave (up) or the highest down an octave (down). Keeps it sorted. */
void voice(int* notes, int count, int position, bool up);

/** Pitch contour followed by Range. New shapes go on the end: `phrase` is saved as an index,
so inserting one would move every saved patch's selection. */
enum Phrase { PHRASE_CADENCE_1, PHRASE_CADENCE_2, PHRASE_CADENCE_3, PHRASE_CADENCE_4, PHRASE_SAW, PHRASE_TRIANGLE, PHRASE_SINE, PHRASE_PULSE, PHRASE_CV, PHRASE_USER, kNumPhrases };
inline constexpr const char* kPhraseNames[kNumPhrases] = {"Cadence 1", "Cadence 2", "Cadence 3", "Cadence 4", "Saw", "Triangle", "Sine", "Pulse", "CV input", "User"};

/** A drawn phrase (PHRASE_USER): `kPhraseColumns` equal columns across the phrase, each
holding a level 0..kPhraseLevels-1, packed four bits per column with column 0 lowest.

Like every other shape it is a contour, not pitches: level 0 is the pool and the top level is
full Range, so Range still decides how far it reaches and the result is snapped to the scale
with everything else. Cadence 1-4 are the same idea at eight columns and five levels. */
using PhraseGrid = uint64_t;
constexpr int kPhraseColumns = 16;
constexpr int kPhraseLevels = 12;

/** Column `col`'s level (0 outside the grid). */
int phraseLevel(PhraseGrid g, int col);
/** `g` with column `col` set to `level`, both clamped. */
PhraseGrid setPhraseLevel(PhraseGrid g, int col, int level);

/** A rising ramp, so User starts as a shape rather than a flat line. */
constexpr PhraseGrid defaultPhraseGrid() {
	PhraseGrid g = 0;
	for (int i = 0; i < kPhraseColumns; i++) {
		// Rounded i / (columns - 1) scaled to the top level.
		PhraseGrid level = PhraseGrid((i * (kPhraseLevels - 1) + (kPhraseColumns - 1) / 2) / (kPhraseColumns - 1));
		g |= level << (4 * i);
	}
	return g;
}
constexpr PhraseGrid kDefaultPhraseGrid = defaultPhraseGrid();

/** Phrase shape (0..1) at `phase` (0..1) through the phrase; `cv` (volts, +-5) feeds
PHRASE_CV and `user` feeds PHRASE_USER. */
double phraseValue(int phrase, double phase, double cv, PhraseGrid user);

} // namespace talea
