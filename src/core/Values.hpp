#pragma once
#include <cstddef>

// Quantized value lists shared by every host (core rule 5). Durations are in quarter notes;
// a bar is 4/4. Rate and spacing lists run long -> short (turning CW = faster); Length runs
// short -> long (turning CW = longer loop).

namespace talea {

struct NamedValue {
	const char* name;
	double quarters; // 0 = infinite
};

template <typename T, size_t N>
constexpr int countOf(const T (&)[N]) {
	return int(N);
}

inline constexpr NamedValue kDivisions[] = {
	{"1/1", 4.0},
	{"1/2", 2.0},
	{"1/2t", 4.0 / 3},
	{"1/4", 1.0},
	{"1/4t", 2.0 / 3},
	{"1/8", 0.5},
	{"1/8t", 1.0 / 3},
	{"1/16", 0.25},
	{"1/16t", 1.0 / 6},
	{"1/32", 0.125},
	{"1/32t", 1.0 / 12},
	{"1/64", 0.0625},
	{"1/64t", 1.0 / 24},
};
inline constexpr int kDefaultDivision = 7; // 1/16

inline constexpr NamedValue kLengths[] = {
	{"1/64", 0.0625},
	{"1/32", 0.125},
	{"1/16", 0.25},
	{"1/8", 0.5},
	{"1/4", 1.0},
	{"1/2", 2.0},
	{"1 bar", 4.0},
	{"2 bars", 8.0},
	{"3 bars", 12.0},
	{"4 bars", 16.0},
	{"5 bars", 20.0},
	{"6 bars", 24.0},
	{"7 bars", 28.0},
	{"8 bars", 32.0},
	{"16 bars", 64.0},
	{"∞", 0.0},
};
inline constexpr int kDefaultLength = 15; // ∞: Steps alone sets the loop

inline constexpr NamedValue kRepeatTimes[] = {
	{"1", 4.0},
	{"1/2", 2.0},
	{"1/2d", 3.0},
	{"1/2t", 4.0 / 3},
	{"1/4", 1.0},
	{"1/4d", 1.5},
	{"1/4t", 2.0 / 3},
	{"1/8", 0.5},
	{"1/8d", 0.75},
	{"1/8t", 1.0 / 3},
	{"1/16", 0.25},
	{"1/16d", 0.375},
	{"1/16t", 1.0 / 6},
	{"1/32", 0.125},
	{"1/64", 0.0625},
};
inline constexpr int kDefaultRepeatTime = 10; // 1/16

inline constexpr NamedValue kRandomRates[] = {
	{"4 bars", 16.0},
	{"2 bars", 8.0},
	{"1 bar", 4.0},
	{"1/2", 2.0},
	{"1/2t", 4.0 / 3},
	{"1/4", 1.0},
	{"1/4t", 2.0 / 3},
	{"1/8", 0.5},
	{"1/8t", 1.0 / 3},
	{"1/16", 0.25},
	{"1/16t", 1.0 / 6},
	{"1/32", 0.125},
	{"1/32t", 1.0 / 12},
	{"1/64", 0.0625},
};
inline constexpr int kDefaultRandomRate = 9; // 1/16

inline constexpr int kMaxRepeats = 48; // one more knob position = infinite

} // namespace talea
