#include "core/twitch_helix.hpp"

#include "core/twitch_auth.hpp"

#include <json.hpp>

namespace goalbar {
namespace {

using nlohmann::json;

std::string string_field(const json &object, const char *key)
{
	const auto it = object.find(key);
	return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::string total_url(std::string_view path, std::string_view broadcaster_id)
{
	return std::string(kHelixEndpoint) + std::string(path) + "?broadcaster_id=" + url_encode(broadcaster_id) +
	       "&first=1";
}

} // namespace

std::string helix_users_url()
{
	return std::string(kHelixEndpoint) + "/users";
}

std::string helix_followers_url(std::string_view broadcaster_id)
{
	return total_url("/channels/followers", broadcaster_id);
}

std::string helix_subscriptions_url(std::string_view broadcaster_id)
{
	return total_url("/subscriptions", broadcaster_id);
}

std::optional<TwitchUser> parse_helix_user(std::string_view text)
{
	const json parsed = json::parse(text.begin(), text.end(), nullptr, false);
	if (parsed.is_discarded() || !parsed.is_object())
		return std::nullopt;
	const auto data = parsed.find("data");
	if (data == parsed.end() || !data->is_array() || data->empty() || !(*data)[0].is_object())
		return std::nullopt;
	const json &first = (*data)[0];
	TwitchUser user{string_field(first, "id"), string_field(first, "login"), string_field(first, "display_name"),
			string_field(first, "broadcaster_type")};
	if (user.id.empty())
		return std::nullopt;
	return user;
}

std::optional<uint64_t> parse_helix_total(std::string_view text)
{
	const json parsed = json::parse(text.begin(), text.end(), nullptr, false);
	if (parsed.is_discarded() || !parsed.is_object())
		return std::nullopt;
	const auto total = parsed.find("total");
	if (total == parsed.end() || !total->is_number_unsigned())
		return std::nullopt;
	return total->get<uint64_t>();
}

std::string encode_twitch_user(const TwitchUser &user)
{
	return json{{"id", user.id},
		    {"login", user.login},
		    {"display_name", user.display_name},
		    {"broadcaster_type", user.broadcaster_type}}
		.dump();
}

std::optional<TwitchUser> decode_twitch_user(std::string_view text)
{
	const json parsed = json::parse(text.begin(), text.end(), nullptr, false);
	if (parsed.is_discarded() || !parsed.is_object())
		return std::nullopt;
	TwitchUser user{string_field(parsed, "id"), string_field(parsed, "login"), string_field(parsed, "display_name"),
			string_field(parsed, "broadcaster_type")};
	if (user.id.empty())
		return std::nullopt;
	return user;
}

} // namespace goalbar
