#pragma once
// Minimal test harness: no dependencies, so the core tests build with a plain compiler.
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace test {

struct Case {
	const char* name;
	void (*fn)();
};

inline std::vector<Case>& registry() {
	static std::vector<Case> cases;
	return cases;
}

inline int& failures() {
	static int n = 0;
	return n;
}

struct Register {
	Register(const char* name, void (*fn)()) {
		registry().push_back({name, fn});
	}
};

template <typename A, typename B>
void checkEq(const A& a, const B& b, const char* exprA, const char* exprB, const char* file, int line) {
	if (a == b)
		return;
	std::ostringstream os;
	os << "  " << file << ":" << line << ": " << exprA << " == " << exprB << "\n"
	   << "      got:      " << a << "\n"
	   << "      expected: " << b << "\n";
	std::fputs(os.str().c_str(), stdout);
	failures()++;
}

} // namespace test

#define TEST_CASE(name) \
	static void name(); \
	static test::Register name##_registration(#name, name); \
	static void name()

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			std::printf("  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
			test::failures()++; \
		} \
	} while (0)

#define CHECK_EQ(a, b) test::checkEq((a), (b), #a, #b, __FILE__, __LINE__)
