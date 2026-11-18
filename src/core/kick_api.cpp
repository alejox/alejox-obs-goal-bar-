#include "core/kick_api.hpp"

#include "core/twitch_auth.hpp" // url_encode

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

// A non-negative count. Kick sends the same field sometimes as a number and
// sometimes as a string of digits ("followers_count": 4 or "4").
std::optional<uint64_t> unsigned_field(const json &object, const char *key)
{
	const auto it = object.find(key);
	if (it == object.end())
		return std::nullopt;
	if (it->is_number_unsigned())
		return it->get<uint64_t>();
	if (it->is_number_integer())
		return it->get<long long>() < 0 ? std::nullopt : std::optional<uint64_t>(it->get<long long>());
	if (!it->is_string())
		return std::nullopt;
	const std::string &text = it->get_ref<const std::string &>();
	if (text.empty() || text.size() > 19)
		return std::nullopt;
	uint64_t value = 0;
	for (char c : text) {
		if (c < '0' || c > '9')
			return std::nullopt;
		value = value * 10 + uint64_t(c - '0');
	}
	return value;
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
	return std::string(kKickPublicEndpoint) + "/channels/" + url_encode(slug);
}

KickChannelLookup parse_kick_channel(int status, std::string_view body)
{
	KickChannelLookup lookup;
	lookup.http_status = status;
	const json parsed = json::parse(body.begin(), body.end(), nullptr, false);
	const bool is_json = !parsed.is_discarded();
	if (status == 404) {
		lookup.status = KickChannelStatus::NotFound;
		return lookup;
	}
	if (status == 429) {
		lookup.status = KickChannelStatus::RateLimited;
		return lookup;
	}
	// Cloudflare answers a blocked client with 403 (or 503 while it runs a
	// challenge) and an HTML page; the endpoint itself only speaks JSON.
	if (status == 403 || (!is_json && (status == 503 || (status >= 200 && status < 300)))) {
		lookup.status = KickChannelStatus::Blocked;
		return lookup;
	}
	if (status < 200 || status >= 300) {
		lookup.status = KickChannelStatus::HttpError;
		return lookup;
	}
	if (!parsed.is_object())
		return lookup; // Malformed

	KickChannel channel;
	channel.followers = unsigned_field(parsed, "followers_count");
	const auto stream = parsed.find("livestream");
	if (stream != parsed.end() && stream->is_object()) {
		const auto live = stream->find("is_live");
		channel.is_live = live != stream->end() && live->is_boolean() && live->get<bool>();
		channel.viewer_count = unsigned_field(*stream, "viewer_count");
	}
	lookup.status = KickChannelStatus::Found;
	lookup.channel = channel;
	return lookup;
}

MetricReading kick_metric_reading(const KickChannelLookup &lookup, Metric metric)
{
	MetricReading reading;
	reading.state = ProviderState::Error;
	switch (lookup.status) {
	case KickChannelStatus::Found:
		break;
	case KickChannelStatus::NotFound:
		reading.message = kKickChannelNotFound;
		return reading;
	case KickChannelStatus::Blocked:
		reading.message = kKickBlocked;
		return reading;
	case KickChannelStatus::RateLimited:
		reading.state = ProviderState::RateLimited;
		reading.message = "Kick rate limit";
		return reading;
	case KickChannelStatus::HttpError:
		reading.message = "HTTP " + std::to_string(lookup.http_status);
		return reading;
	case KickChannelStatus::Malformed:
		reading.message = "unexpected answer from Kick";
		return reading;
	}
	const KickChannel &channel = *lookup.channel;
	if (metric == Metric::Followers) {
		if (!channel.followers) {
			reading.message = "follower count missing in the Kick answer";
			return reading;
		}
		reading.state = ProviderState::Live;
		reading.value = channel.followers;
		return reading;
	}
	if (metric != Metric::Viewers) {
		reading.message = "Kick does not publish this metric";
		return reading;
	}
	if (!channel.is_live) {
		reading.state = ProviderState::NotLive;
		return reading;
	}
	if (!channel.viewer_count) {
		reading.message = "viewer count missing in the Kick answer";
		return reading;
	}
	reading.state = ProviderState::Live;
	reading.value = channel.viewer_count;
	return reading;
}

} // namespace goalbar
