#include "obs/obs_shutdown.hpp"

#include <obs-frontend-api.h>

#include <atomic>

namespace goalbar::obs {
namespace {

std::atomic<bool> shutting_down{false};

// OBS sends SCRIPTING_SHUTDOWN before it clears the scene data on close, and
// EXIT right after.
void on_frontend_event(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN || event == OBS_FRONTEND_EVENT_EXIT)
		shutting_down = true;
}

} // namespace

void watch_obs_shutdown()
{
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
}

void unwatch_obs_shutdown()
{
	shutting_down = true;
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
}

bool obs_shutting_down()
{
	return shutting_down;
}

} // namespace goalbar::obs
