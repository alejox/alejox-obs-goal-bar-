// Provider adapter: the one Twitch account of the process, shared by every
// Goal Bar source. Owns the tokens: the refresh token lives in the OS secret
// store, the access token only in memory. No OBS, no Qt: HTTP, the secret
// store, sleeping and time are injected. Tokens are never logged.
#pragma once

#include "core/cancel_token.hpp"
#include "core/twitch_auth.hpp"
#include "core/twitch_helix.hpp"
#include "providers/http_types.hpp"
#include "secrets/secret_store.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace goalbar {

// Blocking HTTP calls. Both must return promptly (status 0) once `cancel` fires.
struct TwitchHttp {
	std::function<HttpResult(const std::string &url, const HttpHeaders &headers, CancelToken &cancel)> get;
	std::function<HttpResult(const std::string &url, const std::string &form_body, CancelToken &cancel)> post_form;
};

// Waits `delay`; returns false as soon as `cancel` fires.
using CancellableSleep = std::function<bool(std::chrono::seconds delay, CancelToken &cancel)>;
bool sleep_unless_cancelled(std::chrono::seconds delay, CancelToken &cancel);

enum class HelixOutcome {
	Ok,           // Twitch answered; `http.status` may still be an error
	AuthRequired, // no account, or the refresh token was rejected
	Failed,       // no answer (network, cancelled), see `http.error`
};

struct HelixResponse {
	HelixOutcome outcome = HelixOutcome::Failed;
	HttpResult http;
};

struct HelixUserResult {
	HelixOutcome outcome = HelixOutcome::Failed;
	std::optional<TwitchUser> user; // set when outcome is Ok
	std::string error;
};

// What the metric provider needs from the account (a port, so tests can fake it).
class TwitchHelixClient {
public:
	virtual ~TwitchHelixClient() = default;
	// The connected user, fetched once per connection and then cached.
	virtual HelixUserResult current_user(CancelToken &cancel) = 0;
	// Authorized Helix GET. Refreshes the access token shortly before it
	// expires; on 401 it refreshes once more and retries.
	virtual HelixResponse get(const std::string &url, CancelToken &cancel) = 0;
};

class TwitchAccount final : public TwitchHelixClient {
public:
	using Clock = std::chrono::steady_clock;

	// Secret store item names.
	static constexpr const char *kRefreshTokenKey = "twitch.refresh_token";
	static constexpr const char *kUserKey = "twitch.user";

	using Logger = std::function<void(const std::string &message)>;

	TwitchAccount(std::string client_id, TwitchHttp http, std::unique_ptr<SecretStore> secrets, Logger log,
		      CancellableSleep sleep = sleep_unless_cancelled, std::function<Clock::time_point()> now = Clock::now);

	TwitchAccount(const TwitchAccount &) = delete;
	TwitchAccount &operator=(const TwitchAccount &) = delete;

	struct Status {
		bool connected = false;
		std::string display_name; // empty until the user is known
	};
	// Reads the stored account on first use (may touch the secret store).
	Status status();

	// Device Code Grant Flow, step 1: ask Twitch for a user code.
	struct DeviceStart {
		std::optional<TwitchDeviceCode> code;
		std::string error;
	};
	DeviceStart start_connect(CancelToken &cancel);

	// Step 2: poll until the user approves, declines, the code expires or
	// `cancel` fires. On success the account switches to the new tokens.
	enum class ConnectResult { Connected, Expired, Denied, Cancelled, Failed };
	ConnectResult finish_connect(const TwitchDeviceCode &code, CancelToken &cancel, std::string &error);

	// Forgets the tokens and deletes the stored secrets.
	void disconnect();

	HelixUserResult current_user(CancelToken &cancel) override;
	HelixResponse get(const std::string &url, CancelToken &cancel) override;

private:
	enum class Refresh { Done, AuthRequired, Failed };

	struct Snapshot {
		bool has_account = false;
		std::string access_token;
		uint64_t generation = 0;
		bool expiring = false; // the access token is due for a proactive refresh
	};

	Snapshot snapshot();
	Refresh refresh(uint64_t seen_generation, CancelToken &cancel, std::string &error);
	HttpResult helix_request(const std::string &url, const std::string &access_token, CancelToken &cancel);

	// Caller holds mutex_.
	void load_locked();
	void adopt_locked(const TwitchTokens &tokens);
	void forget_locked();

	const std::string client_id_;
	const TwitchHttp http_;
	const std::unique_ptr<SecretStore> secrets_;
	const Logger log_;
	const CancellableSleep sleep_;
	const std::function<Clock::time_point()> now_;

	// One token grant (refresh or device login) at a time: Twitch refresh
	// tokens are one-time-use, so two concurrent refreshes would race.
	std::mutex grant_mutex_;

	std::mutex mutex_; // guards everything below
	bool loaded_ = false;
	std::string access_token_;  // memory only
	std::optional<Clock::time_point> refresh_at_; // proactive refresh, unknown lifetime: none
	std::string refresh_token_; // mirrored in the secret store
	std::optional<TwitchUser> user_;
	uint64_t generation_ = 0; // bumps on every token change or disconnect
};

} // namespace goalbar
