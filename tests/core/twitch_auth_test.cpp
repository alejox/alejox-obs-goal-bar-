#include "core/twitch_auth.hpp"

#include "test_harness.hpp"

using namespace goalbar;
using namespace std::chrono_literals;

namespace {

constexpr const char *kDeviceAnswer = R"({"device_code":"dev-123","expires_in":1800,"interval":5,)"
				      R"("user_code":"ABCDEFGH","verification_uri":"https://www.twitch.tv/activate"})";

TwitchDeviceCode device_code(std::chrono::seconds expires_in = 1800s, std::chrono::seconds interval = 5s)
{
	return TwitchDeviceCode{"dev-123", "ABCDEFGH", "https://www.twitch.tv/activate", expires_in, interval};
}

TwitchTokenResponse token_error(TwitchTokenError error)
{
	TwitchTokenResponse response;
	response.error = error;
	return response;
}

} // namespace

TEST_CASE(form_encoding_escapes_reserved_characters)
{
	CHECK_EQ(url_encode("channel:read:subscriptions"), std::string("channel%3Aread%3Asubscriptions"));
	CHECK_EQ(url_encode("a b+c~-._"), std::string("a%20b%2Bc~-._"));
	CHECK_EQ(form_encode({{"a", "1"}, {"b", "x y"}}), std::string("a=1&b=x%20y"));
}

TEST_CASE(request_bodies_carry_the_public_client_fields)
{
	CHECK_EQ(twitch_device_request_body("cid"), std::string("client_id=cid&scopes=channel%3Aread%3Asubscriptions"));
	CHECK_EQ(twitch_device_token_body("cid", "dev"),
		 std::string("client_id=cid&scopes=channel%3Aread%3Asubscriptions&device_code=dev&grant_type="
			     "urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code"));
	CHECK_EQ(twitch_refresh_body("cid", "r/1"),
		 std::string("client_id=cid&grant_type=refresh_token&refresh_token=r%2F1"));
}

TEST_CASE(device_code_answer_parses)
{
	const auto code = parse_twitch_device_code(kDeviceAnswer);
	REQUIRE(code.has_value());
	CHECK_EQ(code->device_code, std::string("dev-123"));
	CHECK_EQ(code->user_code, std::string("ABCDEFGH"));
	CHECK_EQ(code->verification_uri, std::string("https://www.twitch.tv/activate"));
	CHECK_EQ(code->expires_in.count(), 1800);
	CHECK_EQ(code->interval.count(), 5);

	CHECK(!parse_twitch_device_code(R"({"user_code":"X"})").has_value());
	CHECK(!parse_twitch_device_code("not json").has_value());
}

TEST_CASE(token_answers_parse_success_and_errors)
{
	const TwitchTokenResponse ok = parse_twitch_token_response(
		200, R"({"access_token":"a1","refresh_token":"r1","expires_in":14400,"scope":["x"],"token_type":"bearer"})");
	REQUIRE(ok.tokens.has_value());
	CHECK_EQ(ok.tokens->access_token, std::string("a1"));
	CHECK_EQ(ok.tokens->refresh_token, std::string("r1"));
	CHECK_EQ(ok.tokens->expires_in.count(), 14400);
	CHECK_EQ(ok.error, TwitchTokenError::None);

	CHECK_EQ(parse_twitch_token_response(400, R"({"status":400,"message":"authorization_pending"})").error,
		 TwitchTokenError::AuthorizationPending);
	CHECK_EQ(parse_twitch_token_response(400, R"({"status":400,"message":"slow_down"})").error,
		 TwitchTokenError::SlowDown);
	CHECK_EQ(parse_twitch_token_response(400, R"({"status":400,"message":"invalid device code"})").error,
		 TwitchTokenError::InvalidDeviceCode);
	CHECK_EQ(parse_twitch_token_response(400, R"({"status":400,"message":"access_denied"})").error,
		 TwitchTokenError::AccessDenied);
	CHECK_EQ(parse_twitch_token_response(400, R"({"status":400,"message":"Invalid refresh token"})").error,
		 TwitchTokenError::InvalidGrant);
	CHECK_EQ(parse_twitch_token_response(0, "").error, TwitchTokenError::Network);
	CHECK_EQ(parse_twitch_token_response(500, "oops").error, TwitchTokenError::Other);
	// A 200 without both tokens is not a success.
	CHECK(!parse_twitch_token_response(200, R"({"access_token":"a"})").tokens.has_value());
}

TEST_CASE(device_flow_keeps_polling_while_pending)
{
	const auto t0 = TwitchDeviceFlow::Clock::time_point{};
	TwitchDeviceFlow flow(device_code(), t0);
	CHECK_EQ(flow.first_delay().count(), 5);

	const auto step = flow.on_response(token_error(TwitchTokenError::AuthorizationPending), t0 + 5s);
	CHECK_EQ(step.status, TwitchDeviceFlow::Status::Pending);
	CHECK_EQ(step.next_delay.count(), 5);

	// A network hiccup is transient too.
	CHECK_EQ(flow.on_response(token_error(TwitchTokenError::Network), t0 + 10s).status,
		 TwitchDeviceFlow::Status::Pending);
}

TEST_CASE(device_flow_slows_down_by_five_seconds)
{
	const auto t0 = TwitchDeviceFlow::Clock::time_point{};
	TwitchDeviceFlow flow(device_code(), t0);
	const auto step = flow.on_response(token_error(TwitchTokenError::SlowDown), t0 + 5s);
	CHECK_EQ(step.status, TwitchDeviceFlow::Status::Pending);
	CHECK_EQ(step.next_delay.count(), 10);
	CHECK_EQ(flow.on_response(token_error(TwitchTokenError::AuthorizationPending), t0 + 15s).next_delay.count(), 10);
}

TEST_CASE(device_flow_ends_on_success_denial_and_expiry)
{
	const auto t0 = TwitchDeviceFlow::Clock::time_point{};
	TwitchDeviceFlow flow(device_code(60s), t0);

	TwitchTokenResponse ok;
	ok.tokens = TwitchTokens{"a", "r", 0s};
	CHECK_EQ(flow.on_response(ok, t0 + 5s).status, TwitchDeviceFlow::Status::Succeeded);
	CHECK_EQ(flow.on_response(token_error(TwitchTokenError::AccessDenied), t0 + 5s).status,
		 TwitchDeviceFlow::Status::Denied);
	CHECK_EQ(flow.on_response(token_error(TwitchTokenError::InvalidDeviceCode), t0 + 5s).status,
		 TwitchDeviceFlow::Status::Expired);
	CHECK_EQ(flow.on_response(token_error(TwitchTokenError::Other), t0 + 5s).status,
		 TwitchDeviceFlow::Status::Failed);
	// Pending past the code lifetime: stop instead of polling a dead code.
	CHECK_EQ(flow.on_response(token_error(TwitchTokenError::AuthorizationPending), t0 + 58s).status,
		 TwitchDeviceFlow::Status::Expired);
}

TEST_CASE(token_lifetime_is_optional)
{
	const TwitchTokenResponse ok = parse_twitch_token_response(200, R"({"access_token":"a","refresh_token":"r"})");
	REQUIRE(ok.tokens.has_value());
	CHECK_EQ(ok.tokens->expires_in.count(), 0);
}

TEST_CASE(refresh_deadline_leaves_a_minute_of_margin)
{
	const auto t0 = std::chrono::steady_clock::time_point{};
	CHECK(!twitch_refresh_deadline(t0, 0s).has_value()); // unknown lifetime: rely on 401
	const auto deadline = twitch_refresh_deadline(t0, 14400s);
	REQUIRE(deadline.has_value());
	CHECK(*deadline == t0 + 14340s);
	// Lifetimes shorter than two margins refresh at half time instead.
	CHECK(*twitch_refresh_deadline(t0, 90s) == t0 + 45s);
}
