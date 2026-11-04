#include "Test.hpp"

int main() {
	int failedCases = 0;
	for (const test::Case& c : test::registry()) {
		int before = test::failures();
		c.fn();
		bool ok = test::failures() == before;
		if (!ok)
			failedCases++;
		std::printf("%s %s\n", ok ? "pass" : "FAIL", c.name);
	}
	std::printf("\n%d/%d test cases passed\n", int(test::registry().size()) - failedCases, int(test::registry().size()));
	return failedCases == 0 ? 0 : 1;
}
