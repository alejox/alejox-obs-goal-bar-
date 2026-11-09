#include "providers/kick_account.hpp"

#include <cctype>
#include <utility>

namespace goalbar {
namespace {

std::string trimmed(const std::string &text)
{
	size_t begin = 0;
	size_t end = text.size();
	while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
		++begin;
	while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
		--end;
	return text.substr(begin, end - begin);
}

} // namespace

KickAccount::KickAccount(KickHttp http, std::unique_ptr<SecretStore> secrets, Logger log,
			 std::function<Clock::time_point()> now)
	: http_(std::move(http)),
	  secrets_(std::move(secrets)),
	  log_(std::move(log)),
	  now_(std::move(now))
{
}

void KickAccount::load_locked()
{
	if (loaded_)
		return;
	loaded_ = true;
	const SecretLookup id = secrets_->get(kClientIdKey);
	const SecretLookup secret = secrets_->get(kClientSecretKey);
	for (const SecretLookup *lookup : {&id, &secret})
		if (lookup->error && log_)
			log_("Kick: could not read the stored app credentials: " + lookup->error->message);
	client_id_ = id.secret.value_or("");
	client_secret_ = secret.secret.value_or("");
}

void KickAccount::drop_token_locked()
{
	access_token_.clear();
	renew_at_.reset();
	++generation_;
}

KickAccount::Status KickAccount::status()
{
	std::lock_guard lock(mutex_);
	load_locked();
	Status status;
	status.configured = !client_id_.empty() && !client_secret_.empty();
	status.rejected = status.configured && rejected_;
	status.client_id = client_id_;
	return status;
}

std::optional<SecretError> KickAccount::save_credentials(const std::string &client_id,
							   const std::string &client_secret)
{
	const std::string id = trimmed(client_id);
	const std::string secret = trimmed(client_secret);
	std::lock_guard lock(mutex_);
	load_locked();
	if (const auto error = secrets_->put(kClientIdKey, id))
		return error;
	if (!secret.empty()) {
		if (const auto error = secrets_->put(kClientSecretKey, secret))
			return error;
		client_secret_ = secret;
	}
	client_id_ = id;
	rejected_ = false;
	++credentials_;
	drop_token_locked();
	if (log_)
		log_("Kick: app credentials saved");
	return std::nullopt;
}

void KickAccount::clear_credentials()
{
	std::lock_guard lock(mutex_);
	load_locked();
	client_id_.clear();
	client_secret_.clear();
	rejected_ = false;
	++credentials_;
	drop_token_locked();
	for (const char *key : {kClientIdKey, kClientSecretKey})
		if (const auto error = secrets_->remove(key); error && log_)
			log_("Kick: could not delete a stored secret: " + error->message);
	if (log_)
		log_("Kick: app credentials removed");
}

KickAccount::Snapshot KickAccount::snapshot()
{
	std::lock_guard lock(mutex_);
	load_locked();
	Snapshot state;
	state.configured = !client_id_.empty() && !client_secret_.empty();
	state.rejected = rejected_;
	state.access_token = access_token_;
	state.generation = generation_;
	state.expiring = !access_token_.empty() && renew_at_ && now_() >= *renew_at_;
	return state;
}

KickAccount::Acquire KickAccount::acquire(uint64_t seen_generation, CancelToken &cancel, std::string &error)
{
	std::lock_guard grant(grant_mutex_);
	std::string id;
	std::string secret;
	uint64_t credentials = 0;
	{
		std::lock_guard lock(mutex_);
		load_locked();
		// Someone else got a token (or changed the credentials) meanwhile.
		if (generation_ != seen_generation && !access_token_.empty())
			return Acquire::Done;
		if (client_id_.empty() || client_secret_.empty())
			return Acquire::NotSetUp;
		if (rejected_)
			return Acquire::BadCredentials;
		id = client_id_;
		secret = client_secret_;
		credentials = credentials_;
	}

	const HttpResult result =
		http_.post_form(std::string(kKickTokenUrl), kick_client_credentials_body(id, secret), cancel);
	const KickTokenResponse response = parse_kick_token_response(result.status, result.body);

	std::lock_guard lock(mutex_);
	if (credentials_ != credentials) { // the credentials changed while the request ran
		error = "the Kick app credentials changed";
		return Acquire::Failed;
	}
	if (response.token) {
		access_token_ = response.token->access_token;
		renew_at_ = kick_token_deadline(now_(), response.token->expires_in);
		++generation_;
		return Acquire::Done;
	}
	error = response.message;
	switch (response.error) {
	case KickTokenError::InvalidClient:
		rejected_ = true;
		drop_token_locked();
		if (log_)
			log_("Kick: the app credentials were rejected (" + response.message + ")");
		return Acquire::BadCredentials;
	case KickTokenError::RateLimited:
		return Acquire::RateLimited;
	case KickTokenError::Network:
		error = result.error.empty() ? "no response" : result.error;
		return Acquire::Failed;
	default:
		return Acquire::Failed;
	}
}

HttpResult KickAccount::api_request(const std::string &url, const std::string &token, CancelToken &cancel)
{
	const HttpHeaders headers = {{"Authorization", "Bearer " + token}, {"Accept", "application/json"}};
	return http_.get(url, headers, cancel);
}

KickResponse KickAccount::get(const std::string &url, CancelToken &cancel)
{
	KickResponse response;
	Snapshot state = snapshot();
	if (!state.configured) {
		response.outcome = KickOutcome::NotSetUp;
		return response;
	}
	if (state.rejected) {
		response.outcome = KickOutcome::BadCredentials;
		return response;
	}
	// Gets a token and reloads `state`; false (with `response` filled) on failure.
	auto renew = [&] {
		std::string error;
		const Acquire acquired = acquire(state.generation, cancel, error);
		state = snapshot();
		if (acquired == Acquire::Done && !state.access_token.empty())
			return true;
		switch (acquired) {
		case Acquire::NotSetUp:
			response.outcome = KickOutcome::NotSetUp;
			break;
		case Acquire::BadCredentials:
			response.outcome = KickOutcome::BadCredentials;
			break;
		case Acquire::RateLimited:
			response.outcome = KickOutcome::RateLimited;
			break;
		case Acquire::Done:
		case Acquire::Failed:
			response.outcome = KickOutcome::Failed;
			break;
		}
		response.http = HttpResult{0, "", error};
		return false;
	};
	if (state.access_token.empty() && !renew())
		return response;
	// About to expire: renew first instead of waiting for the 401. If that
	// fails the current token is still tried.
	if (state.expiring) {
		const Snapshot before = state;
		if (!renew()) {
			if (response.outcome == KickOutcome::NotSetUp || response.outcome == KickOutcome::BadCredentials)
				return response;
			state = before;
			response = KickResponse{};
		}
	}
	response.http = api_request(url, state.access_token, cancel);
	// Expired or revoked token: get a new one, then retry once.
	if (response.http.status == 401) {
		{
			std::lock_guard lock(mutex_);
			if (access_token_ == state.access_token)
				drop_token_locked();
			state.generation = generation_;
			state.access_token = access_token_;
		}
		if (state.access_token.empty() && !renew())
			return response;
		response.http = api_request(url, state.access_token, cancel);
	}
	response.outcome = response.http.status == 0 ? KickOutcome::Failed : KickOutcome::Ok;
	return response;
}

KickAccount::TestResult KickAccount::test(CancelToken &cancel, std::string &error)
{
	uint64_t generation = 0;
	{
		std::lock_guard lock(mutex_);
		load_locked();
		if (client_id_.empty() || client_secret_.empty())
			return TestResult::NotSetUp;
		rejected_ = false;
		drop_token_locked();
		generation = generation_;
	}
	switch (acquire(generation, cancel, error)) {
	case Acquire::Done:
		if (log_)
			log_("Kick: app credentials work");
		return TestResult::Ok;
	case Acquire::NotSetUp:
		return TestResult::NotSetUp;
	case Acquire::BadCredentials:
		return TestResult::BadCredentials;
	case Acquire::RateLimited:
		return TestResult::RateLimited;
	case Acquire::Failed:
		break;
	}
	return TestResult::Failed;
}

} // namespace goalbar
