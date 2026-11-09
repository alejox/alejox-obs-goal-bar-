#include "providers/twitch_account.hpp"
#include "secrets/in_memory_secret_store.hpp"

#include "test_harness.hpp"

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace goalbar;
using namespace std::chrono_literals;

namespace {

constexpr const char *kClientId = "cid";
constexpr const char *kUserJson =
	R"({"data":[{"id":"42","login":"streamer","display_name":"Streamer","broadcaster_type":"affiliate"}]})";

std::string tokens_json(const std::string &access, const std::string &refresh)
{
	return R"({"access_token":")" + access + R"(","refresh_token":")" + refresh + R"(","token_type":"bearer"})";
}

std::string header(const HttpHeaders &headers, const std::string &name)
{
	for (const auto &[key, value] : headers)
		if (key == name)
			return value;
	return {};
}

// Scripted Twitch: token POSTs pop `token_answers` (or mint a new pair), and
// Helix GETs succeed only with the latest minted access token.
struct FakeTwitch {
	std::mutex mutex;
	std::deque<HttpResult> token_answers;
	std::vector<std::string> post_bodies;
	std::vector<std::string> get_urls;
	std::string valid_access = "none";
	int minted = 0;
	std::chrono::milliseconds post_delay{0};

	TwitchHttp http()
	{
		return TwitchHttp{
			[this](const std::string &url, const HttpHeaders &headers, CancelToken &) {
				std::lock_guard lock(mutex);
				get_urls.push_back(url);
				if (header(headers, "Client-Id") != kClientId ||
				    header(headers, "Authorization") != "Bearer " + valid_access)
					return HttpResult{401, R"({"status":401,"message":"Invalid OAuth token"})", ""};
				return HttpResult{200, kUserJson, ""};
			},
			[this](const std::string &url, const std::string &body, CancelToken &) {
				std::this_thread::sleep_for(post_delay);
				std::lock_guard lock(mutex);
				post_bodies.push_back(url + " " + body);
				if (url == std::string(kTwitchDeviceUrl))
					return HttpResult{200,
							  R"({"device_code":"dev","user_code":"CODE1234","expires_in":1800,)"
							  R"("interval":5,"verification_uri":"https://www.twitch.tv/activate"})",
							  ""};
				if (!token_answers.empty()) {
					HttpResult answer = token_answers.front();
					token_answers.pop_front();
					return answer;
				}
				++minted;
				valid_access = "a" + std::to_string(minted);
				return HttpResult{200, tokens_json(valid_access, "r" + std::to_string(minted)), ""};
			}};
	}

	size_t posts()
	{
		std::lock_guard lock(mutex);
		return post_bodies.size();
	}
};

struct Fixture {
	FakeTwitch twitch;
	InMemorySecretStore *secrets = nullptr;
	std::vector<std::chrono::seconds> sleeps;
	bool sleep_result = true;
	std::unique_ptr<TwitchAccount> account;

	explicit Fixture(const std::string &stored_refresh = {})
	{
		auto store = std::make_unique<InMemorySecretStore>();
		secrets = store.get();
		if (!stored_refresh.empty())
			store->put(TwitchAccount::kRefreshTokenKey, stored_refresh);
		account = std::make_unique<TwitchAccount>(
			kClientId, twitch.http(), std::move(store), nullptr,
			[this](std::chrono::seconds delay, CancelToken &) {
				sleeps.push_back(delay);
				return sleep_result;
			},
			[] { return TwitchAccount::Clock::time_point{}; });
	}

	std::string stored(const char *key) { return secrets->get(key).secret.value_or(""); }
};

} // namespace

TEST_CASE(account_without_tokens_requires_auth_without_network)
{
	Fixture fx;
	CancelToken cancel;
	CHECK(!fx.account->status().connected);
	CHECK_EQ(fx.account->get(helix_users_url(), cancel).outcome, HelixOutcome::AuthRequired);
	CHECK_EQ(fx.account->current_user(cancel).outcome, HelixOutcome::AuthRequired);
	CHECK_EQ(fx.twitch.posts(), size_t{0});
	CHECK(fx.twitch.get_urls.empty());
}

TEST_CASE(stored_refresh_token_is_exchanged_and_rotated_at_once)
{
	Fixture fx("r0");
	CancelToken cancel;
	CHECK(fx.account->status().connected);
	const HelixResponse response = fx.account->get(helix_users_url(), cancel);
	CHECK_EQ(response.outcome, HelixOutcome::Ok);
	CHECK_EQ(response.http.status, 200);
	REQUIRE(fx.twitch.posts() == 1);
	CHECK_EQ(fx.twitch.post_bodies[0], std::string(kTwitchTokenUrl) + " " + twitch_refresh_body(kClientId, "r0"));
	// One-time-use refresh tokens: the new one is persisted immediately.
	CHECK_EQ(fx.stored(TwitchAccount::kRefreshTokenKey), std::string("r1"));
}

TEST_CASE(unauthorized_answer_refreshes_once_and_retries)
{
	Fixture fx("r0");
	CancelToken cancel;
	fx.account->get(helix_users_url(), cancel); // access token a1
	fx.twitch.valid_access = "expired-elsewhere";
	fx.twitch.minted = 1;
	const HelixResponse response = fx.account->get(helix_users_url(), cancel);
	CHECK_EQ(response.outcome, HelixOutcome::Ok);
	CHECK_EQ(response.http.status, 200);
	CHECK_EQ(fx.twitch.posts(), size_t{2});
	CHECK_EQ(fx.stored(TwitchAccount::kRefreshTokenKey), std::string("r2"));
}

TEST_CASE(rejected_refresh_token_disconnects_the_account)
{
	Fixture fx("r0");
	fx.secrets->put(TwitchAccount::kUserKey, encode_twitch_user({"42", "streamer", "Streamer", ""}));
	fx.twitch.token_answers.push_back({400, R"({"status":400,"message":"Invalid refresh token"})", ""});
	CancelToken cancel;
	CHECK_EQ(fx.account->get(helix_users_url(), cancel).outcome, HelixOutcome::AuthRequired);
	CHECK(!fx.account->status().connected);
	CHECK(!fx.secrets->get(TwitchAccount::kRefreshTokenKey).secret.has_value());
	CHECK(!fx.secrets->get(TwitchAccount::kUserKey).secret.has_value());
}

TEST_CASE(network_failure_during_refresh_keeps_the_account)
{
	Fixture fx("r0");
	fx.twitch.token_answers.push_back({0, "", "offline"});
	CancelToken cancel;
	const HelixResponse response = fx.account->get(helix_users_url(), cancel);
	CHECK_EQ(response.outcome, HelixOutcome::Failed);
	CHECK(fx.account->status().connected);
	CHECK_EQ(fx.stored(TwitchAccount::kRefreshTokenKey), std::string("r0"));
}

TEST_CASE(concurrent_callers_share_one_refresh)
{
	Fixture fx("r0");
	fx.twitch.post_delay = 50ms;
	std::atomic<int> ok{0};
	auto call = [&] {
		CancelToken cancel;
		if (fx.account->get(helix_users_url(), cancel).http.status == 200)
			++ok;
	};
	std::thread first(call), second(call), third(call);
	first.join();
	second.join();
	third.join();
	CHECK_EQ(ok.load(), 3);
	CHECK_EQ(fx.twitch.posts(), size_t{1});
}

TEST_CASE(current_user_is_fetched_once_and_stored)
{
	Fixture fx("r0");
	CancelToken cancel;
	const HelixUserResult user = fx.account->current_user(cancel);
	REQUIRE(user.outcome == HelixOutcome::Ok);
	CHECK_EQ(user.user->id, std::string("42"));
	fx.account->current_user(cancel);
	CHECK_EQ(fx.twitch.get_urls.size(), size_t{1});
	CHECK_EQ(fx.account->status().display_name, std::string("Streamer"));
	CHECK(decode_twitch_user(fx.stored(TwitchAccount::kUserKey)).has_value());
}

TEST_CASE(device_flow_connects_after_pending_and_slow_down)
{
	Fixture fx;
	fx.twitch.token_answers.push_back({400, R"({"status":400,"message":"authorization_pending"})", ""});
	fx.twitch.token_answers.push_back({400, R"({"status":400,"message":"slow_down"})", ""});
	CancelToken cancel;
	const TwitchAccount::DeviceStart start = fx.account->start_connect(cancel);
	REQUIRE(start.code.has_value());
	CHECK_EQ(start.code->user_code, std::string("CODE1234"));
	CHECK_EQ(fx.twitch.post_bodies[0], std::string(kTwitchDeviceUrl) + " " + twitch_device_request_body(kClientId));

	std::string error;
	CHECK_EQ(fx.account->finish_connect(*start.code, cancel, error), TwitchAccount::ConnectResult::Connected);
	REQUIRE(fx.sleeps.size() == 3);
	CHECK_EQ(fx.sleeps[0].count(), 5);
	CHECK_EQ(fx.sleeps[1].count(), 5);
	CHECK_EQ(fx.sleeps[2].count(), 10);
	CHECK_EQ(fx.twitch.post_bodies[1],
		 std::string(kTwitchTokenUrl) + " " + twitch_device_token_body(kClientId, "dev"));
	CHECK_EQ(fx.stored(TwitchAccount::kRefreshTokenKey), std::string("r1"));
	const TwitchAccount::Status status = fx.account->status();
	CHECK(status.connected);
	CHECK_EQ(status.display_name, std::string("Streamer"));
}

TEST_CASE(device_flow_stops_on_cancel_and_denial)
{
	Fixture fx;
	CancelToken cancel;
	const auto code = fx.account->start_connect(cancel).code;
	REQUIRE(code.has_value());
	std::string error;
	fx.sleep_result = false;
	CHECK_EQ(fx.account->finish_connect(*code, cancel, error), TwitchAccount::ConnectResult::Cancelled);
	CHECK_EQ(fx.twitch.posts(), size_t{1}); // only the device request

	fx.sleep_result = true;
	fx.twitch.token_answers.push_back({400, R"({"status":400,"message":"access_denied"})", ""});
	CHECK_EQ(fx.account->finish_connect(*code, cancel, error), TwitchAccount::ConnectResult::Denied);
	CHECK(!fx.account->status().connected);
}

TEST_CASE(disconnect_forgets_tokens_and_secrets)
{
	Fixture fx("r0");
	CancelToken cancel;
	fx.account->current_user(cancel);
	fx.account->disconnect();
	CHECK(!fx.account->status().connected);
	CHECK(!fx.secrets->get(TwitchAccount::kRefreshTokenKey).secret.has_value());
	CHECK(!fx.secrets->get(TwitchAccount::kUserKey).secret.has_value());
	CHECK_EQ(fx.account->get(helix_users_url(), cancel).outcome, HelixOutcome::AuthRequired);
}

TEST_CASE(real_sleep_returns_early_on_cancel)
{
	CancelToken cancel;
	std::thread canceller([&] {
		std::this_thread::sleep_for(20ms);
		cancel.cancel();
	});
	const auto start = std::chrono::steady_clock::now();
	CHECK(!sleep_unless_cancelled(10s, cancel));
	CHECK(std::chrono::steady_clock::now() - start < 1s);
	canceller.join();
	CHECK(!sleep_unless_cancelled(1s, cancel));
}
