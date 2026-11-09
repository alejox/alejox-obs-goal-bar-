#include "core/kick_api.hpp"

#include "core/twitch_auth.hpp" // url_encode, form_encode

#include <json.hpp>

#include <algorithm>
#include <cctype>

namespace goalbar {
namespace {

using nlohmann::json;

std::string lowercase(std::string_view text)
{
	std::string out(text);
	std::transform(out.begin(), out.end(), out.begin(),
		       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return out;
}

bool starts_with(std::string_view text, std::string_view prefix)
{
	return text.substr(0, prefix.size()) == prefix;
}

void strip_prefix(std::string_view &text, std::string_view prefix)
{
	if (starts_with(text, prefix))
		text.remove_prefix(prefix.size());
}

bool is_slug_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

std::optional<uint64_t> unsigned_field(const json &object, const char *key)
{
	const auto it = object.find(key);
	if (it == object.end() || !it->is_number_integer() || it->get<long long>() < 0)
		return std::nullopt;
	return it->get<uint64_t>();
}

KickChannel channel_from(const json &object)
{
	KickChannel channel;
	if (const auto slug = object.find("slug"); slug != object.end() && slug->is_string())
		channel.slug = slug->get<std::string>();
	channel.active_subscribers = unsigned_field(object, "active_subscribers_count");
	const auto stream = object.find("stream");
	if (stream != object.end() && stream->is_object()) {
		const auto live = stream->find("is_live");
		channel.is_live = live != stream->end() && live->is_boolean() && live->get<bool>();
		channel.viewer_count = unsigned_field(*stream, "viewer_count").value_or(0);
	}
	return channel;
}

} // namespace

std::optional<std::string> normalize_kick_slug(std::string_view input)
{
	while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front())))
		input.remove_prefix(1);
	while (!input.empty() && std::isspace(static_cast<unsigned char>(input.back())))
		input.remove_suffix(1);
	const std::string lowered = lowercase(input);
	std::string_view text = lowered;
	strip_prefix(text, "https://");
	strip_prefix(text, "http://");
	strip_prefix(text, "www.");
	strip_prefix(text, "kick.com/");
	strip_prefix(text, "@");
	if (const auto query = text.find_first_of("?#"); query != std::string_view::npos)
		text = text.substr(0, query);
	while (!text.empty() && text.back() == '/')
		text.remove_suffix(1);
	if (text.empty() || text.size() > kKickSlugMaxLength)
		return std::nullopt;
	if (!std::all_of(text.begin(), text.end(), is_slug_char))
		return std::nullopt;
	return std::string(text);
}

std::string kick_channel_url(std::string_view slug)
{
	return std::string(kKickApiEndpoint) + "/channels?slug=" + url_encode(slug);
}

std::string kick_client_credentials_body(std::string_view client_id, std::string_view client_secret)
{
	return form_encode({{"grant_type", "client_credentials"},
			    {"client_id", std::string(client_id)},
			    {"client_secret", std::string(client_secret)}});
}

KickTokenResponse parse_kick_token_response(int status, std::string_view body)
{
	KickTokenResponse response;
	if (status == 0) {
		response.error = KickTokenError::Network;
		response.message = "no response";
		return response;
	}
	const json parsed = json::parse(body.begin(), body.end(), nullptr, false);
	const bool object = !parsed.is_discarded() && parsed.is_object();
	if (object) {
		for (const char *key : {"error_description", "error", "message"}) {
			const auto it = parsed.find(key);
			if (it != parsed.end() && it->is_string() && !it->get<std::string>().empty()) {
				response.message = it->get<std::string>();
				break;
			}
		}
	}
	if (status >= 200 && status < 300 && object) {
		const auto token = parsed.find("access_token");
		if (token != parsed.end() && token->is_string() && !token->get<std::string>().empty()) {
			KickAppToken app;
			app.access_token = token->get<std::string>();
			const auto expires = parsed.find("expires_in");
			if (expires != parsed.end() && expires->is_number_integer() && expires->get<long long>() > 0)
				app.expires_in = std::chrono::seconds(expires->get<long long>());
			response.token = std::move(app);
			response.message.clear();
			return response;
		}
	}
	if (status == 400 || status == 401 || status == 403)
		response.error = KickTokenError::InvalidClient;
	else if (status == 429)
		response.error = KickTokenError::RateLimited;
	else
		response.error = KickTokenError::Other;
	if (response.message.empty())
		response.message = "HTTP " + std::to_string(status);
	return response;
}

std::optional<std::chrono::steady_clock::time_point>
kick_token_deadline(std::chrono::steady_clock::time_point obtained, std::chrono::seconds expires_in)
{
	if (expires_in <= std::chrono::seconds{0})
		return std::nullopt;
	const std::chrono::seconds lead = std::min(kKickTokenMargin, expires_in / 2);
	return obtained + (expires_in - lead);
}

KickChannelLookup parse_kick_channels(std::string_view text, std::string_view slug)
{
	KickChannelLookup lookup;
	const json parsed = json::parse(text.begin(), text.end(), nullptr, false);
	if (parsed.is_discarded() || !parsed.is_object())
		return lookup;
	const auto data = parsed.find("data");
	if (data == parsed.end() || !data->is_array())
		return lookup;
	if (data->empty()) {
		lookup.status = KickChannelStatus::NotFound;
		return lookup;
	}
	const std::string wanted = lowercase(slug);
	const json *match = nullptr;
	for (const json &item : *data) {
		if (!item.is_object())
			continue;
		const auto it = item.find("slug");
		if (it != item.end() && it->is_string() && lowercase(it->get<std::string>()) == wanted) {
			match = &item;
			break;
		}
		if (!match)
			match = &item; // first channel when none matches by slug
	}
	if (!match)
		return lookup;
	lookup.status = KickChannelStatus::Found;
	lookup.channel = channel_from(*match);
	return lookup;
}

MetricReading kick_metric_reading(const KickChannel &channel, Metric metric)
{
	MetricReading reading;
	if (metric == Metric::Subscribers) {
		if (!channel.active_subscribers) {
			reading.state = ProviderState::Error;
			reading.message = "subscriber count missing in the Kick answer";
			return reading;
		}
		reading.state = ProviderState::Live;
		reading.value = channel.active_subscribers;
		return reading;
	}
	if (!channel.is_live) {
		reading.state = ProviderState::NotLive;
		return reading;
	}
	reading.state = ProviderState::Live;
	reading.value = channel.viewer_count;
	return reading;
}

} // namespace goalbar
