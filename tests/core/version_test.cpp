#include "core/version.hpp"

#include "test_harness.hpp"

#include <string>

TEST_CASE(core_version_matches_buildspec)
{
	CHECK_EQ(std::string(goalbar::core_version()), std::string("0.1.1"));
}
