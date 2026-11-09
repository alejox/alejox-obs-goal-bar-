#include "providers/twitch_account.hpp"

#include <condition_variable>
#include <utility>

namespace goalbar {
namespace {

bool is_success(int status)
{
	return status >= 200 && status < 300;
}

std::string describe_failure(const HttpResult &result)
{
	if (result.status == 0)
		return result.error.empty() ? "no response" : result.error;
	return "HTTP " + std::to_string(result.status);
}

} // namespace

bool sleep_unless_cancelled(std::chrono::seconds delay, CancelToken &cancel)
{
	std::mutex mutex;
	std::condition_variable woken;
	bool aborted = false;
	AbortHook hook(&cancel, [&] {
		std::lock_guard lock(mutex);
		aborted = true;
		woken.notify_all();
	});
	if (!hook.armed())
		return false;
	std::unique_lock lock(mutex);
	woken.wait_for(lock, delay, [&] { return aborted; });
	const bool completed = !aborted;
	lock.unlock();
	hook.disarm();
	return completed && !cancel.cancelled();
}

TwitchAccount::TwitchAccount(std::string client_id, TwitchHttp http, std::unique_ptr<SecretStore> secrets,
			     Logger log, CancellableSleep sleep, std::function<Clock::time_point()> now)
	: client_id_(std::move(client_id)),
	  http_(std::move(http)),
	  secrets_(std::move(secrets)),
	  log_(std::move(log)),
	  sleep_(std::move(sleep)),
	  now_(std::move(now))
{
}

void TwitchAccount::load_locked()
{
	if (loaded_)
		return;
	loaded_ = true;
	const SecretLookup refresh = secrets_->get(kRefreshTokenKey);
	if (refresh.error && log_)
		log_("Twitch: could not read the stored account: " + refresh.error->message);
	refresh_token_ = refresh.secret.value_or("");
	if (refresh_token_.empty())
		return;
	const SecretLookup user = secrets_->get(kUserKey);
	if (user.secret)
		user_ = decode_twitch_user(*user.secret);
}

void TwitchAccount::adopt_locked(const TwitchTokens &tokens)
{
	access_token_ = tokens.access_token;
	refresh_token_ = tokens.refresh_token;
	refresh_at_ = twitch_refresh_deadline(now_(), tokens.expires_in);
	++generation_;
	// The previous refresh token is now dead: persist the new one before
	// anything else can fail.
	if (const auto error = secrets_->put(kRefreshTokenKey, refresh_token_); error && log_)
		log_("Twitch: could not save the refresh token: " + error->message);
}

void TwitchAccount::forget_locked()
{
	access_token_.clear();
	refresh_token_.clear();
	refresh_at_.reset();
	user_.reset();
	++generation_;
	for (const char *key : {kRefreshTokenKey, kUserKey})
		if (const auto error = secrets_->remove(key); error && log_)
			log_("Twitch: could not delete a stored secret: " + error->message);
}

TwitchAccount::Status TwitchAccount::status()
{
	std::lock_guard lock(mutex_);
	load_locked();
	Status status;
	status.connected = !refresh_token_.empty();
	if (status.connected && user_)
		status.display_name = user_->display_name.empty() ? user_->login : user_->display_name;
	return status;
}

TwitchAccount::Snapshot TwitchAccount::snapshot()
{
	std::lock_guard lock(mutex_);
	load_locked();
	const bool expiring = !access_token_.empty() && refresh_at_ && now_() >= *refresh_at_;
	return Snapshot{!refresh_token_.empty(), access_token_, generation_, expiring};
}

TwitchAccount::Refresh TwitchAccount::refresh(uint64_t seen_generation, CancelToken &cancel, std::string &error)
{
	std::lock_guard grant(grant_mutex_);
	std::string refresh_token;
	{
		std::lock_guard lock(mutex_);
		if (generation_ != seen_generation) // someone else refreshed (or disconnected) meanwhile
			return refresh_token_.empty() ? Refresh::AuthRequired : Refresh::Done;
		refresh_token = refresh_token_;
	}
	if (refresh_token.empty())
		return Refresh::AuthRequired;

	const HttpResult result =
		http_.post_form(std::string(kTwitchTokenUrl), twitch_refresh_body(client_id_, refresh_token), cancel);
	const TwitchTokenResponse response = parse_twitch_token_response(result.status, result.body);

	std::lock_guard lock(mutex_);
	if (generation_ != seen_generation) // disconnected while the request ran
		return Refresh::AuthRequired;
	if (response.tokens) {
		adopt_locked(*response.tokens);
		return Refresh::Done;
	}
	if (response.error == TwitchTokenError::InvalidGrant || result.status == 400 || result.status == 401) {
		if (log_)
			log_("Twitch: the stored login was rejected (" + response.message + "); reconnect the account");
		forget_locked();
		return Refresh::AuthRequired;
	}
	error = describe_failure(result);
	return Refresh::Failed;
}

HttpResult TwitchAccount::helix_request(const std::string &url, const std::string &access_token, CancelToken &cancel)
{
	const HttpHeaders headers = {{"Client-Id", client_id_}, {"Authorization", "Bearer " + access_token}};
	return http_.get(url, headers, cancel);
}

HelixResponse TwitchAccount::get(const std::string &url, CancelToken &cancel)
{
	HelixResponse response;
	Snapshot state = snapshot();
	if (!state.has_account) {
		response.outcome = HelixOutcome::AuthRequired;
		return response;
	}
	// Refreshes and reloads `state`; false (with `response` filled) on failure.
	auto renew = [&] {
		std::string error;
		const Refresh refreshed = refresh(state.generation, cancel, error);
		state = snapshot();
		if (refreshed == Refresh::Done && !state.access_token.empty())
			return true;
		response.outcome = refreshed == Refresh::Failed ? HelixOutcome::Failed : HelixOutcome::AuthRequired;
		response.http = HttpResult{0, "", error};
		return false;
	};
	// A fresh start has only the refresh token.
	if (state.access_token.empty() && !renew())
		return response;
	// About to expire: refresh first instead of waiting for the 401. If that
	// fails the current token is still tried (and the 401 path stays).
	if (state.expiring) {
		std::string ignored;
		refresh(state.generation, cancel, ignored);
		state = snapshot();
		if (!state.has_account || state.access_token.empty()) {
			response.outcome = HelixOutcome::AuthRequired;
			return response;
		}
	}
	response.http = helix_request(url, state.access_token, cancel);
	// Expired or revoked access token: refresh once, then retry once.
	if (response.http.status == 401) {
		if (!renew())
			return response;
		response.http = helix_request(url, state.access_token, cancel);
	}
	response.outcome = response.http.status == 0 ? HelixOutcome::Failed : HelixOutcome::Ok;
	return response;
}

HelixUserResult TwitchAccount::current_user(CancelToken &cancel)
{
	HelixUserResult result;
	{
		std::lock_guard lock(mutex_);
		load_locked();
		if (user_ && !refresh_token_.empty()) {
			result.outcome = HelixOutcome::Ok;
			result.user = user_;
			return result;
		}
	}
	const HelixResponse response = get(helix_users_url(), cancel);
	result.outcome = response.outcome;
	if (response.outcome != HelixOutcome::Ok) {
		result.error = response.http.error;
		return result;
	}
	result.user = is_success(response.http.status) ? parse_helix_user(response.http.body) : std::nullopt;
	if (!result.user) {
		result.outcome = HelixOutcome::Failed;
		result.error = is_success(response.http.status) ? "unexpected answer from Twitch"
								: describe_failure(response.http);
		return result;
	}
	std::lock_guard lock(mutex_);
	if (!refresh_token_.empty()) { // still connected
		user_ = result.user;
		if (const auto error = secrets_->put(kUserKey, encode_twitch_user(*user_)); error && log_)
			log_("Twitch: could not save the account name: " + error->message);
	}
	return result;
}

TwitchAccount::DeviceStart TwitchAccount::start_connect(CancelToken &cancel)
{
	DeviceStart start;
	const HttpResult result =
		http_.post_form(std::string(kTwitchDeviceUrl), twitch_device_request_body(client_id_), cancel);
	if (!is_success(result.status)) {
		const TwitchTokenResponse failure = parse_twitch_token_response(result.status, result.body);
		start.error = result.status == 0 ? describe_failure(result) : failure.message;
		return start;
	}
	start.code = parse_twitch_device_code(result.body);
	if (!start.code)
		start.error = "unexpected answer from Twitch";
	return start;
}

TwitchAccount::ConnectResult TwitchAccount::finish_connect(const TwitchDeviceCode &code, CancelToken &cancel,
							   std::string &error)
{
	TwitchDeviceFlow flow(code, now_());
	std::chrono::seconds delay = flow.first_delay();
	for (;;) {
		if (!sleep_(delay, cancel))
			return ConnectResult::Cancelled;
		const HttpResult result = http_.post_form(std::string(kTwitchTokenUrl),
							  twitch_device_token_body(client_id_, code.device_code), cancel);
		if (cancel.cancelled())
			return ConnectResult::Cancelled;
		const TwitchTokenResponse response = parse_twitch_token_response(result.status, result.body);
		const TwitchDeviceFlow::Step step = flow.on_response(response, now_());
		switch (step.status) {
		case TwitchDeviceFlow::Status::Pending:
			delay = step.next_delay;
			continue;
		case TwitchDeviceFlow::Status::Expired:
			return ConnectResult::Expired;
		case TwitchDeviceFlow::Status::Denied:
			return ConnectResult::Denied;
		case TwitchDeviceFlow::Status::Failed:
			error = response.message;
			return ConnectResult::Failed;
		case TwitchDeviceFlow::Status::Succeeded:
			break;
		}
		{
			std::lock_guard grant(grant_mutex_);
			std::lock_guard lock(mutex_);
			load_locked();
			forget_locked(); // drops the previous account, if any
			adopt_locked(*response.tokens);
		}
		const HelixUserResult user = current_user(cancel);
		if (user.outcome != HelixOutcome::Ok && log_)
			log_("Twitch: connected, but the account name is not known yet (" + user.error + ")");
		if (log_)
			log_("Twitch: account connected");
		return ConnectResult::Connected;
	}
}

void TwitchAccount::disconnect()
{
	std::lock_guard lock(mutex_);
	load_locked();
	forget_locked();
	if (log_)
		log_("Twitch: account disconnected");
}

} // namespace goalbar
