#include "update/update_settings.hpp"

#include <json.hpp>

namespace goalbar::update {

using nlohmann::json;

std::string encode_update_settings(const UpdateSettings &settings)
{
	const json document = {{"auto_check", settings.auto_check},
			       {"last_check", settings.last_check},
			       {"skipped_version", settings.skipped_version}};
	return document.dump(2);
}

std::optional<UpdateSettings> decode_update_settings(const std::string &text)
{
	const json document = json::parse(text, nullptr, false);
	if (!document.is_object()) {
		return std::nullopt;
	}
	UpdateSettings settings;
	const auto auto_check = document.find("auto_check");
	if (auto_check != document.end() && auto_check->is_boolean()) {
		settings.auto_check = auto_check->get<bool>();
	}
	const auto last_check = document.find("last_check");
	if (last_check != document.end() && last_check->is_number_integer()) {
		settings.last_check = last_check->get<std::int64_t>();
	}
	const auto skipped = document.find("skipped_version");
	if (skipped != document.end() && skipped->is_string()) {
		settings.skipped_version = skipped->get<std::string>();
	}
	return settings;
}

bool automatic_check_due(const UpdateSettings &settings, std::int64_t now)
{
	if (!settings.auto_check) {
		return false;
	}
	return settings.last_check <= 0 || settings.last_check > now ||
	       now - settings.last_check >= kAutomaticCheckIntervalSeconds;
}

bool should_offer(const UpdateSettings &settings, std::string_view version, bool manual)
{
	return manual || settings.skipped_version.empty() || settings.skipped_version != version;
}

} // namespace goalbar::update
