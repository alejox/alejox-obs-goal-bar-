// OBS adapter: the "Goal Bar" input source.
#pragma once

namespace goalbar::obs {

// Registers the source type `goal_bar_source`. Call once from obs_module_load.
void register_goal_bar_source();

} // namespace goalbar::obs
