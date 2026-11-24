#include "providers/chat_count_provider.hpp"
#include "providers/chat_sessions.hpp"

#include "core/kick_api.hpp"
#include "core/tiktok_live.hpp"

#include "test_harness.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace goalbar;
using namespace std::chrono_literals;

namespace {

// A socket that hands out scripted messages, then blocks until closed.
class FakeSocket final : public ChatSocket {
public:
	explicit FakeSocket(std::vector<std::string> script) : script_(script.begin(), script.end()) {}

	bool receive(std::string &message) override
	{
		std::unique_lock lock(mutex_);
		wake_.wait(lock, [this] { return closed_ || !script_.empty(); });
		if (closed_)
			return false;
		message = script_.front();
		script_.pop_front();
		return true;
	}
	bool send(const std::string &text) override
	{
		std::lock_guard lock(mutex_);
		sent_.push_back(text);
		return true;
	}
	bool send_binary(const std::string &data) override
	{
		std::lock_guard lock(mutex_);
		binary_.push_back(data);
		return true;
	}
	std::vector<std::string> sent_binary() const
	{
		std::lock_guard lock(mutex_);
		return binary_;
	}
	void close() override
	{
		std::lock_guard lock(mutex_);
		closed_ = true;
		wake_.notify_all();
	}
	std::vector<std::string> sent() const
	{
		std::lock_guard lock(mutex_);
		return sent_;
	}
	bool sent_contains(const std::string &text) const
	{
		for (const std::string &s : sent())
			if (s == text)
				return true;
		return false;
	}

private:
	mutable std::mutex mutex_;
	std::condition_variable wake_;
	std::deque<std::string> script_;
	std::vector<std::string> sent_;
	std::vector<std::string> binary_;
	bool closed_ = false;
};

struct FakeNet {
	std::shared_ptr<FakeSocket> socket;
	std::shared_ptr<std::string> connected_url = std::make_shared<std::string>();
	std::shared_ptr<HttpHeaders> connected_headers = std::make_shared<HttpHeaders>();
	std::shared_ptr<std::vector<std::string>> urls = std::make_shared<std::vector<std::string>>();
	std::shared_ptr<std::mutex> mutex = std::make_shared<std::mutex>();
	std::function<HttpResult(const std::string &)> answer_get = [](const std::string &) { return HttpResult{}; };
	std::function<HttpResult(const std::string &, const std::string &)> answer_post =
		[](const std::string &, const std::string &) { return HttpResult{}; };

	ChatNet net() const
	{
		ChatNet out;
		out.get = [*this](const std::string &url, const HttpHeaders &, CancelToken &) {
			{
				std::lock_guard lock(*mutex);
				urls->push_back(url);
			}
			return answer_get(url);
		};
		out.post_json = [*this](const std::string &url, const std::string &body, const HttpHeaders &,
					CancelToken &) {
			{
				std::lock_guard lock(*mutex);
				urls->push_back("POST " + url);
			}
			return answer_post(url, body);
		};
		out.connect = [*this](const std::string &url, const HttpHeaders &headers, std::string &) {
			std::lock_guard lock(*mutex);
			*connected_url = url;
			*connected_headers = headers;
			return std::static_pointer_cast<ChatSocket>(socket);
		};
		return out;
	}
};

// Waits until `done(reading)` or two seconds pass.
template<typename F> MetricReading wait_for(const MetricProvider &provider, F done)
{
	const auto deadline = std::chrono::steady_clock::now() + 2s;
	MetricReading reading = provider.latest();
	while (!done(reading) && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(5ms);
		reading = provider.latest();
	}
	return reading;
}

const char *const kTwitchScript =
	":tmi.twitch.tv 001 justinfan1 :Welcome, GLHF!\r\n"
	":justinfan1.tmi.twitch.tv 366 justinfan1 #canal :End of /NAMES list\r\n";

std::vector<std::string> twitch_chat()
{
	return {kTwitchScript,
		"@bits=100;emotes= :ana!ana@ana.tmi.twitch.tv PRIVMSG #canal :Cheer100 vamos\r\n",
		"@emotes= :bob!bob@bob.tmi.twitch.tv PRIVMSG #canal :hi\r\n"
		"@emotes= :eva!eva@eva.tmi.twitch.tv PRIVMSG #canal :qué buen directo\r\n",
		"PING :tmi.twitch.tv\r\n"};
}

TwitchLoginLookup logged_in(const char *login)
{
	return [login](CancelToken &) { return std::optional<std::string>(login); };
}

} // namespace

TEST_CASE(twitch_chat_counts_comments_with_the_minimum)
{
	FakeNet fake;
	fake.socket = std::make_shared<FakeSocket>(twitch_chat());
	ChatCountProvider provider(Platform::Twitch, Metric::Comments, 5,
				   twitch_chat_session(fake.net(), logged_in("canal")), nullptr);
	const MetricReading reading = wait_for(provider, [](const MetricReading &r) { return r.value == 2u; });
	CHECK(reading.state == ProviderState::Live);
	CHECK_EQ(*reading.value, uint64_t{2}); // "Cheer100 vamos" and "qué buen directo"; "hi" is short
	CHECK_EQ(*fake.connected_url, std::string(kTwitchChatUrl));
	CHECK(fake.socket->sent_contains("JOIN #canal"));
	wait_for(provider, [&](const MetricReading &) { return fake.socket->sent_contains("PONG :tmi.twitch.tv"); });
	CHECK(fake.socket->sent_contains("PONG :tmi.twitch.tv"));
}

TEST_CASE(twitch_chat_counts_bits)
{
	FakeNet fake;
	fake.socket = std::make_shared<FakeSocket>(twitch_chat());
	ChatCountProvider provider(Platform::Twitch, Metric::Bits, 0, twitch_chat_session(fake.net(), logged_in("canal")),
				   nullptr);
	const MetricReading reading = wait_for(provider, [](const MetricReading &r) { return r.value == 100u; });
	CHECK_EQ(*reading.value, uint64_t{100});
}

TEST_CASE(twitch_chat_without_an_account_waits)
{
	FakeNet fake;
	fake.socket = std::make_shared<FakeSocket>(twitch_chat());
	ChatCountProvider provider(Platform::Twitch, Metric::Bits, 0,
				   twitch_chat_session(fake.net(), [](CancelToken &) { return std::optional<std::string>(); }),
				   nullptr);
	const MetricReading reading =
		wait_for(provider, [](const MetricReading &r) { return r.state == ProviderState::AuthRequired; });
	CHECK(reading.state == ProviderState::AuthRequired);
	CHECK_EQ(*reading.value, uint64_t{0});
	CHECK(fake.connected_url->empty());
}

// The provider must stop promptly even while the socket waits for a message.
TEST_CASE(chat_provider_stops_promptly)
{
	FakeNet fake;
	fake.socket = std::make_shared<FakeSocket>(std::vector<std::string>{kTwitchScript});
	const auto started = std::chrono::steady_clock::now();
	{
		ChatCountProvider provider(Platform::Twitch, Metric::Comments, 1,
					   twitch_chat_session(fake.net(), logged_in("canal")), nullptr);
		wait_for(provider, [](const MetricReading &r) { return r.state == ProviderState::Live; });
	}
	CHECK(std::chrono::steady_clock::now() - started < 1500ms);
}

TEST_CASE(kick_chat_subscribes_to_the_chatroom_and_counts)
{
	FakeNet fake;
	fake.answer_get = [](const std::string &) {
		return HttpResult{200, R"({"slug":"streamer","chatroom":{"id":668}})", ""};
	};
	fake.answer_post = [](const std::string &, const std::string &) {
		return HttpResult{
			200, R"({"data":{"connections":[{"provider":"pusher","credentials":{"app_key":"k1","cluster":"us2"}}]}})",
			""};
	};
	fake.socket = std::make_shared<FakeSocket>(std::vector<std::string>{
		R"({"event":"pusher:connection_established","data":"{}"})",
		R"({"event":"pusher_internal:subscription_succeeded","data":"{}","channel":"chatrooms.668.v2"})",
		R"({"event":"App\\Events\\ChatMessageEvent","data":"{\"content\":\"hola a todos\"}"})",
		R"({"event":"App\\Events\\ChatMessageEvent","data":"{\"content\":\"[emote:1:KEKW]\"}"})",
		R"({"event":"pusher:ping","data":{}})"});
	ChatCountProvider provider(Platform::Kick, Metric::Comments, 2, kick_chat_session(fake.net(), "streamer"),
				   nullptr);
	const MetricReading reading = wait_for(provider, [](const MetricReading &r) { return r.value == 1u; });
	CHECK(reading.state == ProviderState::Live);
	CHECK_EQ(*reading.value, uint64_t{1}); // the emote alone is one character
	CHECK_EQ(*fake.connected_url,
		 std::string("wss://ws-us2.pusher.com/app/k1?protocol=7&client=js&version=8.4.0&flash=false"));
	CHECK(fake.socket->sent_contains(kick_subscribe_message(668)));
	wait_for(provider, [&](const MetricReading &) { return fake.socket->sent_contains(std::string(kKickPong)); });
	CHECK(fake.socket->sent_contains(std::string(kKickPong)));
}

TEST_CASE(kick_chat_reports_a_missing_channel)
{
	FakeNet fake;
	fake.answer_get = [](const std::string &) { return HttpResult{404, R"({"message":"Channel not found."})", ""}; };
	ChatCountProvider provider(Platform::Kick, Metric::Comments, 1, kick_chat_session(fake.net(), "nadie"), nullptr);
	const MetricReading reading =
		wait_for(provider, [](const MetricReading &r) { return r.state == ProviderState::Error; });
	CHECK(reading.state == ProviderState::Error);
	CHECK_EQ(reading.message, std::string(kKickChannelNotFound));
}

namespace {

const char *const kLivePage = R"(<link rel="canonical" href="https://www.youtube.com/watch?v=4t4AQ15yj1M">)"
			      R"({"isLiveNow":true,"likeCount":"42"})";
const char *const kChatPage = R"("INNERTUBE_API_KEY":"KEY","INNERTUBE_CONTEXT_CLIENT_VERSION":"2.1",)"
			      R"("continuation":"C0")";

std::string chat_batch(const std::string &next, const std::vector<std::string> &texts)
{
	std::string actions;
	for (const std::string &text : texts) {
		if (!actions.empty())
			actions += ",";
		actions += R"({"addChatItemAction":{"item":{"liveChatTextMessageRenderer":{"message":{"runs":[{"text":")" +
			   text + R"("}]}}}}})";
	}
	return R"({"continuationContents":{"liveChatContinuation":{"continuations":[{"timedContinuationData":{"continuation":")" +
	       next + R"(","timeoutMs":1000}}],"actions":[)" + actions + "]}}}";
}

} // namespace

TEST_CASE(youtube_chat_skips_history_and_counts_new_messages)
{
	FakeNet fake;
	fake.answer_get = [](const std::string &url) {
		if (url.find("/live_chat?") != std::string::npos)
			return HttpResult{200, kChatPage, ""};
		return HttpResult{200, kLivePage, ""};
	};
	auto polls = std::make_shared<int>(0);
	fake.answer_post = [polls](const std::string &, const std::string &body) {
		++*polls;
		if (body.find("\"C0\"") != std::string::npos) // history: never counted
			return HttpResult{200, chat_batch("C1", {"mensaje viejo uno", "mensaje viejo dos"}), ""};
		if (body.find("\"C1\"") != std::string::npos)
			return HttpResult{200, chat_batch("C2", {"hola", "saludos desde chile"}), ""};
		return HttpResult{200, chat_batch("C2", {}), ""};
	};
	ChatCountProvider provider(Platform::YouTube, Metric::Comments, 5,
				   youtube_chat_session(fake.net(), "https://www.youtube.com/@x"), nullptr);
	const MetricReading reading = wait_for(provider, [](const MetricReading &r) { return r.value == 1u; });
	CHECK(reading.state == ProviderState::Live);
	CHECK_EQ(*reading.value, uint64_t{1}); // "hola" has 4 characters
	CHECK_EQ(fake.urls->at(0), std::string("https://www.youtube.com/@x/live?hl=en"));
	CHECK_EQ(fake.urls->at(1), std::string("https://www.youtube.com/live_chat?is_popout=1&v=4t4AQ15yj1M"));
	CHECK_EQ(fake.urls->at(2), std::string("POST https://www.youtube.com/youtubei/v1/live_chat/get_live_chat?key=KEY"));
}

TEST_CASE(youtube_chat_waits_while_not_live)
{
	FakeNet fake;
	fake.answer_get = [](const std::string &) {
		return HttpResult{200, R"(<link rel="canonical" href="https://www.youtube.com/@x">)", ""};
	};
	ChatCountProvider provider(Platform::YouTube, Metric::Comments, 1,
				   youtube_chat_session(fake.net(), "https://www.youtube.com/@x"), nullptr);
	const MetricReading reading =
		wait_for(provider, [](const MetricReading &r) { return r.state == ProviderState::NotLive; });
	CHECK(reading.state == ProviderState::NotLive);
	CHECK_EQ(*reading.value, uint64_t{0});
}

TEST_CASE(kick_chat_counts_paid_subscriptions)
{
	FakeNet fake;
	fake.answer_get = [](const std::string &) {
		return HttpResult{200, R"({"slug":"streamer","chatroom":{"id":668}})", ""};
	};
	fake.answer_post = [](const std::string &, const std::string &) { return HttpResult{503, "", ""}; };
	fake.socket = std::make_shared<FakeSocket>(std::vector<std::string>{
		R"({"event":"pusher_internal:subscription_succeeded","data":"{}","channel":"chatrooms.668.v2"})",
		R"({"event":"App\\Events\\ChatMessageEvent","data":"{\"content\":\"hola a todos\"}","channel":"chatrooms.668.v2"})",
		R"({"event":"SubscriptionEvent","data":"{\"chatroom_id\":668,\"username\":\"ana\",\"months\":1}","channel":"chatroom_668"})",
		R"({"event":"GiftedSubscriptionsEvent","data":"{\"chatroom_id\":668,\"gifted_usernames\":[\"a\",\"b\"],\"gifter_username\":\"g\",\"gifted_total\":2}","channel":"chatroom_668"})"});
	ChatCountProvider provider(Platform::Kick, Metric::PaidSubs, 0, kick_chat_session(fake.net(), "streamer"), nullptr);
	const MetricReading reading = wait_for(provider, [](const MetricReading &r) { return r.value == 3u; });
	CHECK_EQ(*reading.value, uint64_t{3});
	// Subscribed to both channels; the fallback Pusher app when the realtime lookup fails.
	CHECK(fake.socket->sent_contains(kick_subscribe_message(668)));
	CHECK(fake.socket->sent_contains(kick_subscribe_messages(668)[1]));
	CHECK_EQ(*fake.connected_url, std::string("wss://ws-us2.pusher.com/app/34bf7a0ff419a2a775b9?protocol=7&client=js&"
						   "version=8.4.0&flash=false"));
}

// A quiet connection gets a ping; with no answer it is closed and reconnected.
TEST_CASE(quiet_kick_chat_is_pinged_then_reconnected)
{
	FakeNet fake;
	fake.answer_get = [](const std::string &) {
		return HttpResult{200, R"({"slug":"streamer","chatroom":{"id":668}})", ""};
	};
	fake.socket = std::make_shared<FakeSocket>(std::vector<std::string>{
		R"({"event":"pusher_internal:subscription_succeeded","data":"{}","channel":"chatrooms.668.v2"})"});
	ChatCountProvider provider(Platform::Kick, Metric::Comments, 1,
				   kick_chat_session(fake.net(), "streamer", ChatKeepAlive{50ms, 150ms}), nullptr);
	const MetricReading reading = wait_for(provider, [](const MetricReading &r) {
		return r.state == ProviderState::Error && r.message.find("quiet") != std::string::npos;
	});
	CHECK(reading.state == ProviderState::Error);
	CHECK_EQ(reading.message, std::string("the Kick chat went quiet; reconnecting"));
	CHECK(fake.socket->sent_contains(std::string(kKickPing)));
}

TEST_CASE(quiet_twitch_chat_is_pinged_then_reconnected)
{
	FakeNet fake;
	fake.socket = std::make_shared<FakeSocket>(std::vector<std::string>{kTwitchScript});
	ChatCountProvider provider(Platform::Twitch, Metric::Comments, 1,
				   twitch_chat_session(fake.net(), logged_in("canal"), ChatKeepAlive{50ms, 150ms}), nullptr);
	const MetricReading reading = wait_for(provider, [](const MetricReading &r) {
		return r.state == ProviderState::Error && r.message.find("quiet") != std::string::npos;
	});
	CHECK_EQ(reading.message, std::string("the Twitch chat went quiet; reconnecting"));
	CHECK(fake.socket->sent_contains("PING :tmi.twitch.tv"));
}

// ---- TikTok -----------------------------------------------------------------

namespace {

constexpr const char *kTikTokLiveRoom =
	R"({"data":{"user":{"uniqueId":"creator","roomId":"7441234567890123456"},"stats":{"followerCount":900},)"
	R"("liveRoom":{"status":2,"liveRoomStats":{"userCount":55}}},"message":"","statusCode":0})";
constexpr const char *kTikTokOfflineRoom =
	R"({"data":{"user":{"uniqueId":"creator","roomId":"7441234567890123456"},"stats":{"followerCount":900},)"
	R"("liveRoom":{"status":4}},"message":"","statusCode":0})";
constexpr const char *kPushServer = "wss://webcast-ws.tiktok.com/webcast/im/ws_proxy/ws_reuse_supplement/";

std::string tiktok_sign_answer()
{
	ProtoWriter route;
	route.bytes(1, "cursor").bytes(2, "c1");
	return ProtoWriter().bytes(7, route.data()).bytes(10, kPushServer).data();
}

std::string tiktok_event(std::string_view method, const std::string &payload)
{
	return ProtoWriter().bytes(1, method).bytes(2, payload).data();
}

// One "msg" frame carrying `events`; it asks for an ack.
std::string tiktok_msg(const std::vector<std::string> &events, uint64_t log_id)
{
	ProtoWriter fetch;
	for (const std::string &event : events)
		fetch.bytes(1, event);
	fetch.bytes(5, "ext").varint(9, 1);
	return ProtoWriter().varint(2, log_id).bytes(6, "pb").bytes(7, "msg").bytes(8, fetch.data()).data();
}

std::string tiktok_comment(std::string_view text)
{
	return tiktok_event("WebcastChatMessage", ProtoWriter().bytes(3, text).data());
}

std::string tiktok_gift(uint64_t diamonds, uint64_t repeat)
{
	return tiktok_event("WebcastGiftMessage",
			    ProtoWriter()
				    .varint(5, repeat)
				    .varint(9, 1)
				    .bytes(15, ProtoWriter().varint(11, 1).varint(12, diamonds).data())
				    .data());
}

std::string tiktok_likes(uint64_t total)
{
	return tiktok_event("WebcastLikeMessage", ProtoWriter().varint(2, 3).varint(3, total).data());
}

std::vector<std::string> tiktok_chat()
{
	return {tiktok_msg({tiktok_comment("hola a todos"), tiktok_gift(5, 3)}, 11),
		tiktok_msg({tiktok_likes(1200), tiktok_comment("ok"), tiktok_gift(1, 1)}, 12),
		tiktok_msg({tiktok_likes(1250)}, 13)};
}

FakeNet tiktok_net(const char *room, HttpResult sign)
{
	FakeNet fake;
	fake.answer_get = [room, sign](const std::string &url) {
		if (url.rfind("https://www.tiktok.com/api-live/user/room/", 0) == 0)
			return HttpResult{200, room, ""};
		if (url.rfind("https://api.eulerstream.com/webcast/rooms/7441234567890123456/connect?", 0) == 0)
			return sign;
		return HttpResult{404, "", ""};
	};
	return fake;
}

const TikTokChatTiming kFastTikTok{std::chrono::milliseconds(20), std::chrono::milliseconds(50),
				   std::chrono::milliseconds(50)};

HttpResult signed_ok()
{
	return HttpResult{200, tiktok_sign_answer(), "", {{"X-Set-TT-Cookie", "ttwid=abc; Path=/; Domain=tiktok.com"}}};
}

ChatSession tiktok_session(const FakeNet &fake)
{
	return tiktok_chat_session(fake.net(), "creator", "Test-Agent/1.0", kFastTikTok);
}

} // namespace

TEST_CASE(tiktok_chat_counts_diamonds_and_acks)
{
	FakeNet fake = tiktok_net(kTikTokLiveRoom, signed_ok());
	fake.socket = std::make_shared<FakeSocket>(tiktok_chat());
	ChatCountProvider provider(Platform::TikTok, Metric::Diamonds, 0, tiktok_session(fake), nullptr);
	const MetricReading reading = wait_for(provider, [](const MetricReading &r) { return r.value == 16u; });
	CHECK(reading.state == ProviderState::Live);
	CHECK_EQ(*reading.value, uint64_t{16}); // 5 × 3 + 1

	// Signed with the same user agent the socket uses, and connected with TikTok's cookies.
	CHECK((*fake.urls)[1].find("user_agent=Test-Agent%2F1.0") != std::string::npos);
	CHECK(fake.connected_url->rfind(std::string(kPushServer) + "?cursor=c1&aid=1988", 0) == 0);
	bool cookie = false, protocol = false;
	for (const auto &[name, value] : *fake.connected_headers) {
		cookie |= name == "Cookie" && value == "tt-target-idc=useast1a; ttwid=abc";
		protocol |= name == "Sec-WebSocket-Protocol" && value == "echo-protocol";
	}
	CHECK(cookie);
	CHECK(protocol);

	const auto sent = [&] { return fake.socket->sent_binary(); };
	wait_for(provider, [&](const MetricReading &) { return sent().size() >= 5; });
	const std::vector<std::string> frames = sent();
	CHECK_EQ(frames.front(), tiktok_enter_room_frame(7441234567890123456ULL));
	int acks = 0, beats = 0;
	for (const std::string &frame : frames) {
		const TikTokFrame parsed = parse_tiktok_frame(frame);
		acks += parsed.type == "ack";
		beats += parsed.type == "hb";
		if (parsed.type == "ack")
			CHECK_EQ(parsed.payload, std::string("ext"));
	}
	CHECK_EQ(acks, 3);
	CHECK(beats >= 1);
}

TEST_CASE(tiktok_chat_counts_comments_and_room_likes)
{
	FakeNet fake = tiktok_net(kTikTokLiveRoom, signed_ok());
	fake.socket = std::make_shared<FakeSocket>(tiktok_chat());
	ChatCountProvider comments(Platform::TikTok, Metric::Comments, 3, tiktok_session(fake), nullptr);
	CHECK(wait_for(comments, [](const MetricReading &r) { return r.value == 1u; }).value == 1u); // "ok" is short

	FakeNet fake2 = tiktok_net(kTikTokLiveRoom, signed_ok());
	fake2.socket = std::make_shared<FakeSocket>(tiktok_chat());
	ChatCountProvider likes(Platform::TikTok, Metric::Likes, 0, tiktok_session(fake2), nullptr);
	CHECK(!likes.latest().value.has_value() || likes.latest().value == 1200u || likes.latest().value == 1250u);
	CHECK(wait_for(likes, [](const MetricReading &r) { return r.value == 1250u; }).value == 1250u);
}

TEST_CASE(tiktok_chat_waits_while_not_live)
{
	FakeNet fake = tiktok_net(kTikTokOfflineRoom, signed_ok());
	fake.socket = std::make_shared<FakeSocket>(tiktok_chat());
	ChatCountProvider provider(Platform::TikTok, Metric::Diamonds, 0, tiktok_session(fake), nullptr);
	const MetricReading reading =
		wait_for(provider, [](const MetricReading &r) { return r.state == ProviderState::NotLive; });
	CHECK(reading.state == ProviderState::NotLive);
	CHECK(fake.connected_url->empty());
	std::lock_guard lock(*fake.mutex);
	for (const std::string &url : *fake.urls)
		CHECK(url.find("eulerstream") == std::string::npos); // no sign request is spent
}

TEST_CASE(tiktok_sign_limit_is_reported)
{
	FakeNet fake = tiktok_net(kTikTokLiveRoom, HttpResult{429, "", ""});
	fake.socket = std::make_shared<FakeSocket>(tiktok_chat());
	ChatCountProvider provider(Platform::TikTok, Metric::Comments, 0, tiktok_session(fake), nullptr);
	const MetricReading reading =
		wait_for(provider, [](const MetricReading &r) { return r.state == ProviderState::RateLimited; });
	CHECK(reading.state == ProviderState::RateLimited);
	CHECK_EQ(reading.message, std::string(kTikTokSignLimit));
	CHECK(fake.connected_url->empty());
}

TEST_CASE(tiktok_unknown_user_is_an_error)
{
	FakeNet fake;
	fake.answer_get = [](const std::string &) {
		return HttpResult{200, R"({"data":null,"message":"user_not_found","statusCode":19881007})", ""};
	};
	ChatCountProvider provider(Platform::TikTok, Metric::Comments, 0, tiktok_session(fake), nullptr);
	const MetricReading reading =
		wait_for(provider, [](const MetricReading &r) { return r.state == ProviderState::Error; });
	CHECK_EQ(reading.message, std::string(kTikTokUserNotFound));
}

TEST_CASE(tiktok_stream_end_waits_for_the_next_live)
{
	FakeNet fake = tiktok_net(kTikTokLiveRoom, signed_ok());
	fake.socket = std::make_shared<FakeSocket>(std::vector<std::string>{
		tiktok_msg({tiktok_gift(10, 1), tiktok_event("WebcastControlMessage", ProtoWriter().varint(2, 3).data())},
			   1)});
	ChatCountProvider provider(Platform::TikTok, Metric::Diamonds, 0, tiktok_session(fake), nullptr);
	const MetricReading reading = wait_for(provider, [](const MetricReading &r) {
		return r.value == 10u && r.state == ProviderState::NotLive;
	});
	CHECK_EQ(*reading.value, uint64_t{10});
	CHECK(reading.state == ProviderState::NotLive);
}
