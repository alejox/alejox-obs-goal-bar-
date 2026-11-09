// Pure core: no OBS, no Qt. Twitch OAuth for a public client: the Device Code
// Grant Flow plus refresh-token grants (no client secret). Builds the form
// bodies and parses the responses; the adapters do the HTTP.
#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace goalbar {

inline constexpr std::string_view kTwitchDeviceUrl = "https://id.twitch.tv/oauth2/device";
inline constexpr std::string_view kTwitchTokenUrl = "https://id.twitch.tv/oauth2/token";
inline constexpr std::string_view kTwitchScopes = "channel:read:subscriptions";

using FormFields = std::vector<std::pair<std::string, std::string>>;

// application/x-www-form-urlencoded (RFC 3986 unreserved characters kept).
std::string url_encode(std::string_view text);
std::string form_encode(const FormFields &fields);

std::string twitch_device_request_body(std::string_view client_id);
std::string twitch_device_token_body(std::string_view client_id, std::string_view device_code);
std::string twitch_refresh_body(std::string_view client_id, std::string_view refresh_token);

// POST /oauth2/device answer. Never log `device_code`.
struct TwitchDeviceCode {
	std::string device_code;
	std::string user_code;
	std::string verification_uri;
	std::chrono::seconds expires_in{0};
	std::chrono::seconds interval{5};
};

// Nullopt unless the body has device_code, user_code and verification_uri.
std::optional<TwitchDeviceCode> parse_twitch_device_code(std::string_view json);

struct TwitchTokens {
	std::string access_token;
	std::string refresh_token;
	std::chrono::seconds expires_in{0}; // access token lifetime, 0 when not sent
};

// Refresh the access token this long before it expires.
inline constexpr std::chrono::seconds kTwitchRefreshMargin{60};

// When an access token obtained at `obtained` should be refreshed
// proactively: kTwitchRefreshMargin before expiry, or at half its lifetime
// when that is shorter. Nullopt when the lifetime is unknown (rely on 401).
std::optional<std::chrono::steady_clock::time_point>
twitch_refresh_deadline(std::chrono::steady_clock::time_point obtained, std::chrono::seconds expires_in);

enum class TwitchTokenError {
	None,
	Network,              // no HTTP response
	AuthorizationPending, // keep polling
	SlowDown,             // keep polling, 5 s slower
	InvalidDeviceCode,    // the device code expired or was already used
	AccessDenied,         // the user declined
	InvalidGrant,         // refresh token rejected: the account must reconnect
	Other,
};

struct TwitchTokenResponse {
	std::optional<TwitchTokens> tokens; // set only on success
	TwitchTokenError error = TwitchTokenError::None;
	std::string message; // Twitch's error text, never a token
};

// `status` 0 means no response (TwitchTokenError::Network).
TwitchTokenResponse parse_twitch_token_response(int status, std::string_view body);

// Polling schedule of one device authorization, driven by injected time.
class TwitchDeviceFlow {
public:
	using Clock = std::chrono::steady_clock;

	enum class Status { Pending, Succeeded, Expired, Denied, Failed };

	struct Step {
		Status status = Status::Pending;
		std::chrono::seconds next_delay{0}; // meaningful while Pending
	};

	TwitchDeviceFlow(const TwitchDeviceCode &code, Clock::time_point now);

	// Delay before the first token poll.
	std::chrono::seconds first_delay() const { return interval_; }

	// Decides what to do after one token poll answered at `now`.
	Step on_response(const TwitchTokenResponse &response, Clock::time_point now);

	std::chrono::seconds interval() const { return interval_; }

private:
	Step pending(Clock::time_point now) const;

	std::chrono::seconds interval_;
	Clock::time_point deadline_;
};

} // namespace goalbar
