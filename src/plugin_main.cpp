/*
obs-goal-bar
Copyright (C) 2026 alejox

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

// Composition root: wires the core to its OBS and Qt adapters.

#include "core/version.hpp"
#include "obs/goal_bar_source.hpp"
#include "obs/obs_shutdown.hpp"
#include "ui/update_controller.hpp"
#include "update/staging.hpp"
#include "update/update_check.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <filesystem>
#include <memory>
#include <string>
#include <utility>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

// Lets OBS finish starting, and the user see the main window, before the network check.
constexpr int kUpdateCheckDelayMs = 8000;

std::unique_ptr<goalbar::ui::UpdateController> updates;

// In UTF-8, as libobs returns it. Convert with u8path: on Windows a std::filesystem::path
// built from a plain string assumes the system code page instead.
std::string module_config_path(const char *file)
{
	char *path = obs_module_config_path(file);
	const std::string result = path != nullptr ? path : "";
	bfree(path);
	return result;
}

std::unique_ptr<goalbar::ui::UpdateController> make_update_controller()
{
	goalbar::update::UpdateServiceConfig config;
	config.current_version = PLUGIN_VERSION;
	config.staging_dir = std::filesystem::u8path(module_config_path("update"));
	config.result_file = std::filesystem::u8path(module_config_path("update-result.txt"));
	config.platform = goalbar::update::current_update_platform();
	const char *binary = obs_get_module_binary_path(obs_current_module());
	if (binary != nullptr && config.platform) {
		if (const auto root = goalbar::update::install_root_from_binary(binary, *config.platform)) {
			config.install_root = std::filesystem::u8path(*root);
		}
	}
	if (!config.install_root) {
		obs_log(LOG_INFO, "updates can only be installed manually from %s", binary ? binary : "(unknown)");
	}
	return std::make_unique<goalbar::ui::UpdateController>(
		std::move(config), std::filesystem::u8path(module_config_path("update.json")));
}

void on_frontend_event(enum obs_frontend_event event, void *)
{
	if (event != OBS_FRONTEND_EVENT_EXIT || !updates)
		return;
	// The staged update installs from here rather than from obs_module_unload: Qt and the
	// main loop still exist, so QProcess can launch the installer, and OBS is certain to be
	// closing. The installer only starts work once this process is gone, and launching it
	// returns at once. An OBS crash sends no EXIT; the package then stays staged for the
	// next close.
	updates->launch_staged_install();
	updates.reset();
}

} // namespace

bool obs_module_load(void)
{
	goalbar::obs::watch_obs_shutdown();
	goalbar::obs::register_goal_bar_source();
	updates = make_update_controller();
	updates->start(kUpdateCheckDelayMs);
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	obs_log(LOG_INFO, "loaded (version %s)", goalbar::core_version());
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	updates.reset();
	goalbar::obs::unwatch_obs_shutdown();
	obs_log(LOG_INFO, "unloaded");
}
