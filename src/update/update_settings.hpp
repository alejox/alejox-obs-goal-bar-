// What the plugin remembers about updates, kept in its own small file (update.json).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace goalbar::update {

// The automatic check runs at most once per this many seconds (24 hours).
inline constexpr std::int64_t kAutomaticCheckIntervalSeconds = 24 * 60 * 60;

struct UpdateSettings {
	// Check GitHub for a newer release when OBS starts (at most once a day).
	bool auto_check = true;
	// Unix seconds of the last check that reached the network; 0 when never.
	std::int64_t last_check = 0;
	// A version the user chose to skip; the automatic check stays quiet about it.
	std::string skipped_version;
};

std::string encode_update_settings(const UpdateSettings &settings);

// nullopt when the text is not a settings object; missing fields keep their defaults.
std::optional<UpdateSettings> decode_update_settings(const std::string &text);

// Whether the startup check should contact GitHub at `now` (Unix seconds): automatic checks
// are on and the last one is a day old, or lies in the future (the clock was set back).
bool automatic_check_due(const UpdateSettings &settings, std::int64_t now);

// Whether the automatic check should offer `version`: anything except the skipped version.
// A manual check always offers.
bool should_offer(const UpdateSettings &settings, std::string_view version, bool manual);

} // namespace goalbar::update
