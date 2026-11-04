#include "Test.hpp"
#include "core/Euclid.hpp"

#include <algorithm>

using namespace talea;

// "x..x..x." style string, step 0 first.
static std::string str(StepMask mask, int steps) {
	std::string s;
	for (int i = 0; i < steps; i++)
		s += hasHit(mask, i) ? 'x' : '.';
	return s;
}

static int popcount(StepMask m) {
	int n = 0;
	for (; m; m &= m - 1)
		n++;
	return n;
}

TEST_CASE(euclid_known_rhythms) {
	// Toussaint, "The Euclidean Algorithm Generates Traditional Musical Rhythms" (2005).
	CHECK_EQ(str(euclid(5, 2), 5), "x.x..");
	CHECK_EQ(str(euclid(5, 3), 5), "x.x.x");
	CHECK_EQ(str(euclid(7, 4), 7), "x.x.x.x");
	CHECK_EQ(str(euclid(8, 3), 8), "x..x..x.");
	CHECK_EQ(str(euclid(8, 5), 8), "x.xx.xx.");
	CHECK_EQ(str(euclid(9, 4), 9), "x.x.x.x..");
	CHECK_EQ(str(euclid(9, 5), 9), "x.x.x.x.x");
	CHECK_EQ(str(euclid(12, 5), 12), "x..x.x..x.x.");
	CHECK_EQ(str(euclid(12, 7), 12), "x.xx.x.xx.x.");
	CHECK_EQ(str(euclid(16, 5), 16), "x..x..x..x..x...");
	CHECK_EQ(str(euclid(16, 4), 16), "x...x...x...x...");
}

TEST_CASE(euclid_edge_cases) {
	CHECK_EQ(euclid(16, 0), StepMask(0));
	CHECK_EQ(euclid(0, 0), StepMask(0));
	CHECK_EQ(euclid(16, 16), fullMask(16));
	CHECK_EQ(euclid(64, 64), ~StepMask(0));
	CHECK_EQ(str(euclid(1, 1), 1), "x");
	// Clamping
	CHECK_EQ(euclid(16, 99), fullMask(16));
	CHECK_EQ(euclid(16, -3), StepMask(0));
	CHECK_EQ(euclid(200, 64), ~StepMask(0));
}

TEST_CASE(euclid_is_even_for_all_sizes) {
	for (int steps = 1; steps <= kMaxSteps; steps++) {
		for (int pulses = 1; pulses <= steps; pulses++) {
			StepMask m = euclid(steps, pulses);
			CHECK_EQ(popcount(m), pulses);
			CHECK((m & ~fullMask(steps)) == 0);
			CHECK(hasHit(m, 0));
			// Gaps between consecutive hits (wrapping) differ by at most one step.
			int minGap = steps, maxGap = 0, first = -1, prev = -1;
			for (int i = 0; i < steps; i++) {
				if (!hasHit(m, i))
					continue;
				if (prev >= 0) {
					minGap = std::min(minGap, i - prev);
					maxGap = std::max(maxGap, i - prev);
				}
				else {
					first = i;
				}
				prev = i;
			}
			int wrap = first + steps - prev;
			minGap = std::min(minGap, wrap);
			maxGap = std::max(maxGap, wrap);
			if (maxGap - minGap > 1) {
				std::printf("    uneven: steps=%d pulses=%d %s\n", steps, pulses, str(m, steps).c_str());
				CHECK(maxGap - minGap <= 1);
			}
		}
	}
}

TEST_CASE(rotate_moves_hits_later) {
	StepMask m = euclid(8, 3); // x..x..x.
	CHECK_EQ(str(rotate(m, 8, 1), 8), ".x..x..x");
	CHECK_EQ(str(rotate(m, 8, 2), 8), "x.x..x..");
	CHECK_EQ(str(rotate(m, 8, -1), 8), "..x..x.x");
	CHECK_EQ(rotate(m, 8, 8), m);
	CHECK_EQ(rotate(m, 8, 0), m);
	CHECK_EQ(rotate(m, 8, 17), rotate(m, 8, 1));
	StepMask full = fullMask(64);
	CHECK_EQ(rotate(full, 64, 5), full);
	CHECK_EQ(rotate(StepMask(1), 64, 63), StepMask(1) << 63);
	CHECK_EQ(rotate(StepMask(1) << 63, 64, 1), StepMask(1));
}
