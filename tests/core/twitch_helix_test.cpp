#include "core/twitch_helix.hpp"

#include "test_harness.hpp"

using namespace goalbar;

TEST_CASE(helix_urls)
{
	CHECK_EQ(helix_users_url(), std::string("https://api.twitch.tv/helix/users"));
	CHECK_EQ(helix_followers_url("123"),
		 std::string("https://api.twitch.tv/helix/channels/followers?broadcaster_id=123&first=1"));
	CHECK_EQ(helix_subscriptions_url("123"),
		 std::string("https://api.twitch.tv/helix/subscriptions?broadcaster_id=123&first=1"));
}

TEST_CASE(helix_user_parses_the_first_user)
{
	const auto user = parse_helix_user(
		R"({"data":[{"id":"141981764","login":"twitchdev","display_name":"TwitchDev","type":"",)"
		R"("broadcaster_type":"partner","description":"x"}]})");
	REQUIRE(user.has_value());
	CHECK_EQ(user->id, std::string("141981764"));
	CHECK_EQ(user->login, std::string("twitchdev"));
	CHECK_EQ(user->display_name, std::string("TwitchDev"));
	CHECK(user->can_have_subscribers());

	const auto plain = parse_helix_user(R"({"data":[{"id":"1","login":"a","display_name":"A","broadcaster_type":""}]})");
	REQUIRE(plain.has_value());
	CHECK(!plain->can_have_subscribers());

	CHECK(!parse_helix_user(R"({"data":[]})").has_value());
	CHECK(!parse_helix_user("<html>").has_value());
}

TEST_CASE(helix_total_parses)
{
	CHECK_EQ(parse_helix_total(R"({"total":8,"data":[{"user_id":"1"}],"pagination":{}})").value_or(0), uint64_t{8});
	CHECK_EQ(parse_helix_total(R"({"data":[],"total":0,"points":0})").value_or(99), uint64_t{0});
	CHECK(!parse_helix_total(R"({"data":[]})").has_value());
	CHECK(!parse_helix_total(R"({"total":-1})").has_value());
	CHECK(!parse_helix_total("").has_value());
}

TEST_CASE(twitch_user_round_trips_through_its_text_form)
{
	const TwitchUser user{"1", "login", "Display Name", "affiliate"};
	const auto decoded = decode_twitch_user(encode_twitch_user(user));
	REQUIRE(decoded.has_value());
	CHECK_EQ(decoded->id, user.id);
	CHECK_EQ(decoded->login, user.login);
	CHECK_EQ(decoded->display_name, user.display_name);
	CHECK_EQ(decoded->broadcaster_type, user.broadcaster_type);
	CHECK(!decode_twitch_user("").has_value());
}
