// Provider adapter: the one Kick app of the process, shared by every Goal Bar
// source. The user brings their own Kick app (kick.com/settings/developer):
// its client ID and secret live only in the OS secret store, the app access
// token (client credentials grant) only in memory. No OBS, no Qt: HTTP, the
// secret store and time are injected. Secrets and tokens are never logged.
#pragma once

#include "core/cancel_token.hpp"
#include "core/kick_api.hpp"
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
struct KickHttp {
	std::function<HttpResult(const std::string &url, const HttpHeaders &headers, CancelToken &cancel)> get;
	std::function<HttpResult(const std::string &url, const std::string &form_body, CancelToken &cancel)> post_form;
};

enum class KickOutcome {
	Ok,             // Kick answered the API call; `http.status` may still be an error
	NotSetUp,       // no client ID or secret stored
	BadCredentials, // the token endpoint rejected the client ID or secret
	RateLimited,    // the token endpoint answered 429
	Failed,         // no answer (network, cancelled), see `http.error`
};

struct KickResponse {
	KickOutcome outcome = KickOutcome::Failed;
	HttpResult http;
	// Seconds from a Retry-After header, when the transport reports headers.
	std::optional<std::chrono::seconds> retry_after;
};

// What the metric provider needs from the account (a port, so tests can fake it).
class KickApiClient {
public:
	virtual ~KickApiClient() = default;
	// Authorized GET on the public API. Gets an app token first when there is
	// none or it is about to expire; on 401 it gets a new one and retries once.
	virtual KickResponse get(const std::string &url, CancelToken &cancel) = 0;
};

class KickAccount final : public KickApiClient {
public:
	using Clock = std::chrono::steady_clock;
	using Logger = std::function<void(const std::string &message)>;

	// Secret store item names.
	static constexpr const char *kClientIdKey = "kick.client_id";
	static constexpr const char *kClientSecretKey = "kick.client_secret";

	KickAccount(KickHttp http, std::unique_ptr<SecretStore> secrets, Logger log,
		    std::function<Clock::time_point()> now = Clock::now);

	KickAccount(const KickAccount &) = delete;
	KickAccount &operator=(const KickAccount &) = delete;

	struct Status {
		bool configured = false; // client ID and secret are stored
		bool rejected = false;   // Kick rejected them; fixed only by saving new ones
		std::string client_id;   // not a secret; shown in the dialog
	};
	// Reads the stored credentials on first use (may touch the secret store).
	Status status();

	// Stores the credentials. An empty `client_secret` keeps the stored one.
	// Drops the cached token, so the next call uses the new credentials.
	std::optional<SecretError> save_credentials(const std::string &client_id, const std::string &client_secret);

	// Forgets the credentials and the token and deletes the stored secrets.
	void clear_credentials();

	// Requests a fresh app token with the stored credentials.
	enum class TestResult { Ok, NotSetUp, BadCredentials, RateLimited, Failed };
	TestResult test(CancelToken &cancel, std::string &error);

	KickResponse get(const std::string &url, CancelToken &cancel) override;

private:
	enum class Acquire { Done, NotSetUp, BadCredentials, RateLimited, Failed };

	struct Snapshot {
		bool configured = false;
		bool rejected = false;
		std::string access_token;
		uint64_t generation = 0;
		bool expiring = false;
	};

	Snapshot snapshot();
	Acquire acquire(uint64_t seen_generation, CancelToken &cancel, std::string &error);
	HttpResult api_request(const std::string &url, const std::string &token, CancelToken &cancel);

	// Caller holds mutex_.
	void load_locked();
	void drop_token_locked();

	const KickHttp http_;
	const std::unique_ptr<SecretStore> secrets_;
	const Logger log_;
	const std::function<Clock::time_point()> now_;

	// One token request at a time: concurrent 401s share one new token.
	std::mutex grant_mutex_;

	std::mutex mutex_; // guards everything below
	bool loaded_ = false;
	std::string client_id_;
	std::string client_secret_; // memory copy of the stored secret
	bool rejected_ = false;
	std::string access_token_; // memory only
	std::optional<Clock::time_point> renew_at_;
	uint64_t generation_ = 0;  // bumps on every token or credentials change
	uint64_t credentials_ = 0; // bumps on every credentials change
};

} // namespace goalbar
