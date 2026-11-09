// OBS adapter: knows when the OBS frontend is shutting down, so teardown code
// can skip work that needs the UI thread (it may never run again).
#pragma once

namespace goalbar::obs {

// Starts listening to the frontend events. Call from obs_module_load.
void watch_obs_shutdown();
// Stops listening. Call from obs_module_unload.
void unwatch_obs_shutdown();

// True from the moment OBS starts closing (scripting shutdown or exit).
bool obs_shutting_down();

} // namespace goalbar::obs
