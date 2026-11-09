#include "core/compact_number.hpp"

#include "test_harness.hpp"

#include <cstdint>
#include <limits>
#include <string>

using namespace goalbar;

TEST_CASE(values_below_one_thousand_are_plain)
{
	CHECK_EQ(format_compact(0), std::string("0"));
	CHECK_EQ(format_compact(7), std::string("7"));
	CHECK_EQ(format_compact(999), std::string("999"));
}

TEST_CASE(thousands_keep_three_significant_figures)
{
	CHECK_EQ(format_compact(1000), std::string("1K"));
	CHECK_EQ(format_compact(1234), std::string("1.23K"));
	CHECK_EQ(format_compact(1500), std::string("1.5K"));
	CHECK_EQ(format_compact(1050), std::string("1.05K"));
	CHECK_EQ(format_compact(12345), std::string("12.3K"));
	CHECK_EQ(format_compact(123456), std::string("123K"));
}

TEST_CASE(larger_units)
{
	CHECK_EQ(format_compact(1000000), std::string("1M"));
	CHECK_EQ(format_compact(1250000), std::string("1.25M"));
	CHECK_EQ(format_compact(2000000000), std::string("2B"));
	CHECK_EQ(format_compact(uint64_t{3400000000000}), std::string("3.4T"));
}

TEST_CASE(truncates_instead_of_rounding_up)
{
	// A count never displays as more than it is.
	CHECK_EQ(format_compact(1999), std::string("1.99K"));
	CHECK_EQ(format_compact(999999), std::string("999K"));
}

TEST_CASE(huge_values_stay_in_trillions)
{
	CHECK_EQ(format_compact(std::numeric_limits<uint64_t>::max()), std::string("18446744T"));
}
