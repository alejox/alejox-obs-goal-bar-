#include "core/version.hpp"

#ifndef GOAL_BAR_VERSION
#error "GOAL_BAR_VERSION must be defined by the build"
#endif

namespace goalbar {

const char *core_version()
{
	return GOAL_BAR_VERSION;
}

} // namespace goalbar
