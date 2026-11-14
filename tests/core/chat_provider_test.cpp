#include "providers/chat_count_provider.hpp"
#include "providers/chat_sessions.hpp"

#include "core/kick_api.hpp"

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
	bool closed_ = false;
};

struct FakeNet {
	std::shared_ptr<FakeSocket> socket;
	std::shared_ptr<std::string> connected_url = std::make_shared<std::string>();
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
		out.connect = [*this](const std::string &url, const HttpHeaders &, std::string &) {
			std::lock_guard lock(*mutex);
			*connected_url = url;
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
