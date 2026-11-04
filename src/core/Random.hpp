#pragma once
#include <cstdint>

namespace talea {

/** xoshiro128** seeded through splitmix64. The core owns its PRNG (rather than using the
host's) so a seeded random sequence is identical in every host and on every platform. */
class Random {
public:
	explicit Random(uint64_t seed = 0) {
		reseed(seed);
	}

	void reseed(uint64_t seed) {
		for (int i = 0; i < 4; i += 2) {
			uint64_t v = splitmix64(seed);
			s_[i] = uint32_t(v);
			s_[i + 1] = uint32_t(v >> 32);
		}
	}

	uint32_t next() {
		uint32_t result = rotl(s_[1] * 5, 7) * 9;
		uint32_t t = s_[1] << 9;
		s_[2] ^= s_[0];
		s_[3] ^= s_[1];
		s_[1] ^= s_[2];
		s_[0] ^= s_[3];
		s_[2] ^= t;
		s_[3] = rotl(s_[3], 11);
		return result;
	}

	/** Uniform in [0, 1). */
	float uniform() {
		return float(next() >> 8) * (1.0f / 16777216.0f);
	}

	/** Uniform integer in [0, n). Returns 0 for n <= 0. */
	int below(int n) {
		if (n <= 0)
			return 0;
		return int((uint64_t(next()) * uint64_t(n)) >> 32);
	}

	/** Stateless uniform in [0, 1) from a key: the same key always gives the same value, so
	a decision keyed on a note's position replays identically after reset or seek. */
	static double hashUniform(uint64_t key) {
		return double(splitmix64(key) >> 11) * (1.0 / 9007199254740992.0);
	}

	/** Advances `state` and returns the next splitmix64 output. */
	static uint64_t splitmix64(uint64_t& state) {
		uint64_t z = (state += 0x9E3779B97F4A7C15ull);
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
		return z ^ (z >> 31);
	}

private:
	static uint32_t rotl(uint32_t x, int k) {
		return (x << k) | (x >> (32 - k));
	}

	uint32_t s_[4];
};

} // namespace talea
