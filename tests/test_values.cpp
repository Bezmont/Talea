#include "Test.hpp"
#include "core/Values.hpp"

#include <string>

using namespace talea;

TEST_CASE(value_list_defaults) {
	CHECK_EQ(std::string(kDivisions[kDefaultDivision].name), "1/16");
	CHECK_EQ(std::string(kLengths[kDefaultLength].name), "∞");
	CHECK_EQ(std::string(kRepeatTimes[kDefaultRepeatTime].name), "1/16");
	CHECK_EQ(std::string(kRandomRates[kDefaultRandomRate].name), "1/16");
	CHECK_EQ(countOf(kRepeatTimes), 15);
}

TEST_CASE(value_list_ordering) {
	// Rates run long -> short, Length short -> long (finite entries).
	for (int i = 1; i < countOf(kDivisions); i++)
		CHECK(kDivisions[i].quarters < kDivisions[i - 1].quarters);
	for (int i = 1; i < countOf(kRandomRates); i++)
		CHECK(kRandomRates[i].quarters < kRandomRates[i - 1].quarters);
	for (int i = 1; i < countOf(kLengths) - 1; i++)
		CHECK(kLengths[i].quarters > kLengths[i - 1].quarters);
	CHECK_EQ(kLengths[countOf(kLengths) - 1].quarters, 0.0);
}
