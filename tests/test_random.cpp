#include "Test.hpp"
#include "core/Random.hpp"

using namespace talea;

TEST_CASE(splitmix64_reference_value) {
	// First output for state 0, from the reference implementation (Vigna).
	uint64_t state = 0;
	CHECK_EQ(Random::splitmix64(state), 0xE220A8397B1DCDAFull);
}

TEST_CASE(random_is_deterministic_per_seed) {
	Random a(1234), b(1234), c(1235);
	bool differs = false;
	for (int i = 0; i < 100; i++) {
		uint32_t va = a.next();
		CHECK_EQ(va, b.next());
		if (va != c.next())
			differs = true;
	}
	CHECK(differs);

	a.reseed(42);
	b.reseed(42);
	CHECK_EQ(a.next(), b.next());
}

TEST_CASE(random_ranges) {
	Random r(7);
	int counts[5] = {};
	for (int i = 0; i < 10000; i++) {
		float u = r.uniform();
		CHECK(u >= 0.f && u < 1.f);
		int k = r.below(5);
		CHECK(k >= 0 && k < 5);
		counts[k]++;
	}
	for (int k = 0; k < 5; k++)
		CHECK(counts[k] > 1800 && counts[k] < 2200);
	CHECK_EQ(r.below(0), 0);
	CHECK_EQ(r.below(1), 0);
}
