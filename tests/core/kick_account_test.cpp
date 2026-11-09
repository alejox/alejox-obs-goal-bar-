#include "providers/kick_account.hpp"
#include "secrets/in_memory_secret_store.hpp"

#include "test_harness.hpp"

#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace goalbar;
using namespace std::chrono_literals;

namespace {

constexpr const char *kId = "client-1";
constexpr const char *kSecret = "very-secret";
const std::string kUrl = kick_channel_url("streamer");
constexpr const char *kChannelJson = R"({"data":[{"slug":"streamer","stream":{"is_live":true,"viewer_count":5}}]})";

std::string header(const HttpHeaders &headers, const std::string &name)
{
	for (const auto &[key, value] : headers)
		if (key == name)
			return value;
	return {};
}

// Scripted Kick: token POSTs pop `token_answers` (or mint a new token for the
// right credentials), API GETs succeed only with the latest minted token.
struct FakeKick {
	std::mutex mutex;
	std::deque<HttpResult> token_answers;
	std::vector<std::string> post_bodies;
	std::vector<HttpHeaders> get_headers;
	std::string valid_token = "none";
	int minted = 0;
	int expires_in = 3600;
	std::chrono::milliseconds post_delay{0};

	KickHttp http()
	{
		return KickHttp{[this](const std::string &, const HttpHeaders &headers, CancelToken &) {
					std::lock_guard lock(mutex);
					get_headers.push_back(headers);
					if (header(headers, "Authorization") != "Bearer " + valid_token)
						return HttpResult{401, R"({"message":"Unauthorized"})", ""};
					return HttpResult{200, kChannelJson, ""};
				},
				[this](const std::string &url, const std::string &body, CancelToken &) {
					std::this_thread::sleep_for(post_delay);
					std::lock_guard lock(mutex);
					post_bodies.push_back(url + " " + body);
					if (!token_answers.empty()) {
						HttpResult answer = token_answers.front();
						token_answers.pop_front();
						return answer;
					}
					if (body != kick_client_credentials_body(kId, kSecret))
						return HttpResult{400, R"({"error":"Invalid request"})", ""};
					++minted;
					valid_token = "t" + std::to_string(minted);
					return HttpResult{200,
							  R"({"access_token":")" + valid_token +
								  R"(","token_type":"Bearer","expires_in":)" +
								  std::to_string(expires_in) + "}",
							  ""};
				}};
	}

	size_t posts()
	{
		std::lock_guard lock(mutex);
		return post_bodies.size();
	}
	size_t gets()
	{
		std::lock_guard lock(mutex);
		return get_headers.size();
	}
};

struct Fixture {
	FakeKick kick;
	InMemorySecretStore *secrets = nullptr;
	KickAccount::Clock::time_point now{};
	std::vector<std::string> logs;
	std::unique_ptr<KickAccount> account;

	explicit Fixture(bool stored = true)
	{
		auto store = std::make_unique<InMemorySecretStore>();
		secrets = store.get();
		if (stored) {
			store->put(KickAccount::kClientIdKey, kId);
			store->put(KickAccount::kClientSecretKey, kSecret);
		}
		account = std::make_unique<KickAccount>(
			kick.http(), std::move(store), [this](const std::string &line) { logs.push_back(line); },
			[this] { return now; });
	}

	std::string stored(const char *key) { return secrets->get(key).secret.value_or(""); }
};

} // namespace

TEST_CASE(kick_without_credentials_is_not_set_up_without_network)
{
	Fixture fx(false);
	CancelToken cancel;
	CHECK(!fx.account->status().configured);
	CHECK_EQ(fx.account->get(kUrl, cancel).outcome, KickOutcome::NotSetUp);
	std::string error;
	CHECK_EQ(fx.account->test(cancel, error), KickAccount::TestResult::NotSetUp);
	CHECK_EQ(fx.kick.posts(), size_t{0});
	CHECK_EQ(fx.kick.gets(), size_t{0});
}

TEST_CASE(kick_saved_credentials_go_to_the_secret_store)
{
	Fixture fx(false);
	CHECK(!fx.account->save_credentials("  client-1 ", kSecret).has_value());
	CHECK_EQ(fx.stored(KickAccount::kClientIdKey), std::string(kId));
	CHECK_EQ(fx.stored(KickAccount::kClientSecretKey), std::string(kSecret));
	const KickAccount::Status status = fx.account->status();
	CHECK(status.configured);
	CHECK_EQ(status.client_id, std::string(kId));
	// An empty secret keeps the stored one.
	CHECK(!fx.account->save_credentials(kId, "").has_value());
	CHECK_EQ(fx.stored(KickAccount::kClientSecretKey), std::string(kSecret));
}

TEST_CASE(kick_app_token_is_requested_once_and_reused)
{
	Fixture fx;
	CancelToken cancel;
	const KickResponse first = fx.account->get(kUrl, cancel);
	CHECK_EQ(first.outcome, KickOutcome::Ok);
	CHECK_EQ(first.http.status, 200);
	CHECK_EQ(fx.account->get(kUrl, cancel).http.status, 200);
	REQUIRE(fx.kick.posts() == 1);
	CHECK_EQ(fx.kick.post_bodies[0],
		 std::string(kKickTokenUrl) + " " + kick_client_credentials_body(kId, kSecret));
	CHECK_EQ(header(fx.kick.get_headers[0], "Authorization"), std::string("Bearer t1"));
	CHECK_EQ(header(fx.kick.get_headers[0], "Accept"), std::string("application/json"));
}

TEST_CASE(kick_token_is_renewed_before_it_expires)
{
	Fixture fx;
	CancelToken cancel;
	fx.account->get(kUrl, cancel);
	fx.now += 3299s; // still inside the margin
	fx.account->get(kUrl, cancel);
	CHECK_EQ(fx.kick.posts(), size_t{1});
	fx.now += 2s; // 3301 s: within 300 s of the 3600 s lifetime
	const KickResponse renewed = fx.account->get(kUrl, cancel);
	CHECK_EQ(renewed.http.status, 200);
	CHECK_EQ(fx.kick.posts(), size_t{2});
	CHECK_EQ(fx.kick.gets(), size_t{3}); // no 401 round trip
}

TEST_CASE(kick_unauthorized_answer_gets_a_new_token_and_retries_once)
{
	Fixture fx;
	CancelToken cancel;
	fx.account->get(kUrl, cancel);
	{
		std::lock_guard lock(fx.kick.mutex);
		fx.kick.valid_token = "revoked";
	}
	const KickResponse response = fx.account->get(kUrl, cancel);
	CHECK_EQ(response.http.status, 200);
	CHECK_EQ(fx.kick.posts(), size_t{2});
	CHECK_EQ(fx.kick.gets(), size_t{3});
}

TEST_CASE(kick_concurrent_unauthorized_answers_share_one_new_token)
{
	Fixture fx;
	CancelToken cancel;
	fx.account->get(kUrl, cancel);
	{
		std::lock_guard lock(fx.kick.mutex);
		fx.kick.valid_token = "revoked";
		fx.kick.post_delay = 50ms;
	}
	std::vector<std::thread> threads;
	std::vector<int> statuses(3, 0);
	for (int i = 0; i < 3; ++i)
		threads.emplace_back([&, i] {
			CancelToken own;
			statuses[i] = fx.account->get(kUrl, own).http.status;
		});
	for (auto &thread : threads)
		thread.join();
	for (int status : statuses)
		CHECK_EQ(status, 200);
	CHECK_EQ(fx.kick.posts(), size_t{2});
}

TEST_CASE(kick_rejected_credentials_stop_until_new_ones_are_saved)
{
	Fixture fx;
	CancelToken cancel;
	fx.kick.token_answers.push_back(HttpResult{401, R"({"error":"invalid_client"})", ""});
	CHECK_EQ(fx.account->get(kUrl, cancel).outcome, KickOutcome::BadCredentials);
	CHECK(fx.account->status().rejected);
	// No more token requests while rejected.
	CHECK_EQ(fx.account->get(kUrl, cancel).outcome, KickOutcome::BadCredentials);
	CHECK_EQ(fx.kick.posts(), size_t{1});
	CHECK_EQ(fx.kick.gets(), size_t{0});
	// Saving (even the same) credentials clears the rejection.
	fx.account->save_credentials(kId, kSecret);
	CHECK(!fx.account->status().rejected);
	CHECK_EQ(fx.account->get(kUrl, cancel).http.status, 200);
}

TEST_CASE(kick_token_rate_limit_and_network_failures)
{
	Fixture fx;
	CancelToken cancel;
	fx.kick.token_answers.push_back(HttpResult{429, "", ""});
	CHECK_EQ(fx.account->get(kUrl, cancel).outcome, KickOutcome::RateLimited);
	fx.kick.token_answers.push_back(HttpResult{0, "", "timed out"});
	const KickResponse failed = fx.account->get(kUrl, cancel);
	CHECK_EQ(failed.outcome, KickOutcome::Failed);
	CHECK_EQ(failed.http.error, std::string("timed out"));
	CHECK(!fx.account->status().rejected);
	CHECK_EQ(fx.account->get(kUrl, cancel).outcome, KickOutcome::Ok);
}

TEST_CASE(kick_test_requests_a_fresh_token)
{
	Fixture fx;
	CancelToken cancel;
	std::string error;
	CHECK_EQ(fx.account->test(cancel, error), KickAccount::TestResult::Ok);
	CHECK_EQ(fx.account->test(cancel, error), KickAccount::TestResult::Ok);
	CHECK_EQ(fx.kick.posts(), size_t{2});
	fx.account->save_credentials(kId, "wrong");
	CHECK_EQ(fx.account->test(cancel, error), KickAccount::TestResult::BadCredentials);
	CHECK_EQ(error, std::string("Invalid request"));
}

TEST_CASE(kick_clear_forgets_everything)
{
	Fixture fx;
	CancelToken cancel;
	fx.account->get(kUrl, cancel);
	fx.account->clear_credentials();
	CHECK(!fx.account->status().configured);
	CHECK(!fx.secrets->get(KickAccount::kClientIdKey).secret.has_value());
	CHECK(!fx.secrets->get(KickAccount::kClientSecretKey).secret.has_value());
	CHECK_EQ(fx.account->get(kUrl, cancel).outcome, KickOutcome::NotSetUp);
}

TEST_CASE(kick_logs_never_carry_the_secret_or_token)
{
	Fixture fx;
	CancelToken cancel;
	fx.account->get(kUrl, cancel);
	fx.account->save_credentials(kId, "other-secret");
	fx.account->get(kUrl, cancel);
	fx.account->clear_credentials();
	CHECK(!fx.logs.empty());
	for (const std::string &line : fx.logs) {
		CHECK(line.find(kSecret) == std::string::npos);
		CHECK(line.find("other-secret") == std::string::npos);
		CHECK(line.find("t1") == std::string::npos);
	}
}
