#include "core/twitch_auth.hpp"

#include <json.hpp>

#include <algorithm>
#include <cctype>

namespace goalbar {
namespace {

using nlohmann::json;
using namespace std::chrono_literals;

constexpr std::string_view kDeviceGrantType = "urn:ietf:params:oauth:grant-type:device_code";
constexpr std::chrono::seconds kSlowDownStep = 5s;

std::string lowercase(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(),
		       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return text;
}

std::string string_field(const json &object, const char *key)
{
	const auto it = object.find(key);
	return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::chrono::seconds seconds_field(const json &object, const char *key, std::chrono::seconds fallback)
{
	const auto it = object.find(key);
	if (it == object.end() || !it->is_number_integer() || it->get<long long>() <= 0)
		return fallback;
	return std::chrono::seconds(it->get<long long>());
}

std::optional<json> parse_object(std::string_view text)
{
	json parsed = json::parse(text.begin(), text.end(), nullptr, false);
	if (parsed.is_discarded() || !parsed.is_object())
		return std::nullopt;
	return parsed;
}

TwitchTokenError classify_error(const std::string &message)
{
	const std::string text = lowercase(message);
	if (text.find("authorization_pending") != std::string::npos)
		return TwitchTokenError::AuthorizationPending;
	if (text.find("slow_down") != std::string::npos)
		return TwitchTokenError::SlowDown;
	if (text.find("invalid device code") != std::string::npos || text.find("expired_token") != std::string::npos)
		return TwitchTokenError::InvalidDeviceCode;
	if (text.find("access_denied") != std::string::npos || text.find("denied") != std::string::npos)
		return TwitchTokenError::AccessDenied;
	if (text.find("refresh token") != std::string::npos || text.find("invalid_grant") != std::string::npos)
		return TwitchTokenError::InvalidGrant;
	return TwitchTokenError::Other;
}

} // namespace

std::string url_encode(std::string_view text)
{
	static constexpr char kHex[] = "0123456789ABCDEF";
	std::string out;
	out.reserve(text.size());
	for (const char c : text) {
		const auto byte = static_cast<unsigned char>(c);
		if (std::isalnum(byte) || c == '-' || c == '.' || c == '_' || c == '~') {
			out += c;
		} else {
			out += '%';
			out += kHex[byte >> 4];
			out += kHex[byte & 0x0F];
		}
	}
	return out;
}

std::string form_encode(const FormFields &fields)
{
	std::string body;
	for (const auto &[name, value] : fields) {
		if (!body.empty())
			body += '&';
		body += url_encode(name) + '=' + url_encode(value);
	}
	return body;
}

std::string twitch_device_request_body(std::string_view client_id)
{
	return form_encode({{"client_id", std::string(client_id)}, {"scopes", std::string(kTwitchScopes)}});
}

std::string twitch_device_token_body(std::string_view client_id, std::string_view device_code)
{
	return form_encode({{"client_id", std::string(client_id)},
			    {"scopes", std::string(kTwitchScopes)},
			    {"device_code", std::string(device_code)},
			    {"grant_type", std::string(kDeviceGrantType)}});
}

std::string twitch_refresh_body(std::string_view client_id, std::string_view refresh_token)
{
	return form_encode({{"client_id", std::string(client_id)},
			    {"grant_type", "refresh_token"},
			    {"refresh_token", std::string(refresh_token)}});
}

std::optional<TwitchDeviceCode> parse_twitch_device_code(std::string_view text)
{
	const std::optional<json> object = parse_object(text);
	if (!object)
		return std::nullopt;
	TwitchDeviceCode code;
	code.device_code = string_field(*object, "device_code");
	code.user_code = string_field(*object, "user_code");
	code.verification_uri = string_field(*object, "verification_uri");
	code.expires_in = seconds_field(*object, "expires_in", 1800s);
	code.interval = seconds_field(*object, "interval", 5s);
	if (code.device_code.empty() || code.user_code.empty() || code.verification_uri.empty())
		return std::nullopt;
	return code;
}

TwitchTokenResponse parse_twitch_token_response(int status, std::string_view body)
{
	TwitchTokenResponse response;
	if (status == 0) {
		response.error = TwitchTokenError::Network;
		return response;
	}
	const std::optional<json> object = parse_object(body);
	if (status >= 200 && status < 300 && object) {
		TwitchTokens tokens{string_field(*object, "access_token"), string_field(*object, "refresh_token")};
		if (!tokens.access_token.empty() && !tokens.refresh_token.empty()) {
			response.tokens = std::move(tokens);
			return response;
		}
	}
	if (object) {
		response.message = string_field(*object, "message");
		if (response.message.empty())
			response.message = string_field(*object, "error");
	}
	response.error = classify_error(response.message);
	if (response.message.empty())
		response.message = "HTTP " + std::to_string(status);
	return response;
}

TwitchDeviceFlow::TwitchDeviceFlow(const TwitchDeviceCode &code, Clock::time_point now)
	: interval_(std::max(code.interval, std::chrono::seconds(1))),
	  deadline_(now + code.expires_in)
{
}

TwitchDeviceFlow::Step TwitchDeviceFlow::pending(Clock::time_point now) const
{
	if (now + interval_ >= deadline_)
		return {Status::Expired, {}};
	return {Status::Pending, interval_};
}

TwitchDeviceFlow::Step TwitchDeviceFlow::on_response(const TwitchTokenResponse &response, Clock::time_point now)
{
	if (response.tokens)
		return {Status::Succeeded, {}};
	switch (response.error) {
	case TwitchTokenError::SlowDown:
		interval_ += kSlowDownStep;
		return pending(now);
	case TwitchTokenError::AuthorizationPending:
	case TwitchTokenError::Network:
		return pending(now);
	case TwitchTokenError::InvalidDeviceCode:
		return {Status::Expired, {}};
	case TwitchTokenError::AccessDenied:
		return {Status::Denied, {}};
	default:
		return {Status::Failed, {}};
	}
}

} // namespace goalbar
