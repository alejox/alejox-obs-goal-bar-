// Provider adapter: counts bits or comments from a live chat while the source
// runs. The platform part is a ChatSession (see chat_sessions.hpp); the network
// is injected, so no OBS, no Qt, and the tests can drive it with fakes.
#pragma once

#include "core/cancel_token.hpp"
#include "core/chat_events.hpp"
#include "core/metric_provider.hpp"
#include "providers/http_types.hpp"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace goalbar {

// A client WebSocket (text messages). receive() blocks until a message
// arrives and returns false once the connection ends; close() may be called
// from any thread and ends a pending receive().
class ChatSocket {
public:
	virtual ~ChatSocket() = default;
	virtual bool receive(std::string &message) = 0;
	virtual bool send(const std::string &text) = 0;
	virtual void close() = 0;
};

// The network a chat session uses. Blocking calls; HTTP ones must return
// promptly (status 0) once `cancel` fires.
struct ChatNet {
	std::function<HttpResult(const std::string &url, const HttpHeaders &headers, CancelToken &cancel)> get;
	std::function<HttpResult(const std::string &url, const std::string &json, const HttpHeaders &headers,
				 CancelToken &cancel)>
		post_json;
	// Null with `error` set when the connection fails.
	std::function<std::shared_ptr<ChatSocket>(const std::string &url, const HttpHeaders &headers,
						  std::string &error)>
		connect;
};

// What a session sees of the provider that runs it.
class ChatSessionHost {
public:
	virtual ~ChatSessionHost() = default;
	virtual void set_state(ProviderState state, const std::string &message = {}) = 0;
	virtual void add(const ChatEvent &event) = 0;
	virtual bool stopping() const = 0;
	// Sleeps up to `duration`; true when the provider is stopping.
	virtual bool wait(std::chrono::milliseconds duration) = 0;
	// The socket in use: the provider closes it when it stops.
	virtual void attach(std::shared_ptr<ChatSocket> socket) = 0;
	virtual CancelToken &cancel() = 0;
};

// One connection to a chat. Returns when the connection ends: an empty
// string for a normal end (the provider reconnects soon), or an error message
// (the provider shows it and retries with a growing delay).
using ChatSession = std::function<std::string(ChatSessionHost &host)>;

class ChatCountProvider final : public MetricProvider, private ChatSessionHost {
public:
	using Logger = std::function<void(const std::string &message)>;

	// `metric` is Bits or Comments; a message counts as a comment with at
	// least `min_chars` characters. The total starts at zero and the session
	// starts at once. `retry` is the first delay after a failure.
	ChatCountProvider(Platform platform, Metric metric, int min_chars, ChatSession session, Logger log,
			  std::chrono::milliseconds retry = std::chrono::seconds(5));
	// Closes the socket, cancels HTTP in flight and joins the worker.
	~ChatCountProvider() override;

	ChatCountProvider(const ChatCountProvider &) = delete;
	ChatCountProvider &operator=(const ChatCountProvider &) = delete;

	Platform platform() const override { return platform_; }
	Metric metric() const override { return metric_; }
	// The value is the total counted since the provider started.
	MetricReading latest() const override;

private:
	void run();

	void set_state(ProviderState state, const std::string &message) override;
	void add(const ChatEvent &event) override;
	bool stopping() const override;
	bool wait(std::chrono::milliseconds duration) override;
	void attach(std::shared_ptr<ChatSocket> socket) override;
	CancelToken &cancel() override { return cancel_; }

	const Platform platform_;
	const Metric metric_;
	const ChatSession session_;
	const Logger log_;
	const std::chrono::milliseconds retry_;

	mutable std::mutex mutex_;
	std::condition_variable wake_;
	bool stop_ = false;
	bool reached_live_ = false; // this session got as far as Live
	CancelToken cancel_;
	std::shared_ptr<ChatSocket> socket_;
	ChatTally tally_;
	MetricReading reading_;
	std::thread worker_; // last member: starts after everything above exists
};

} // namespace goalbar
