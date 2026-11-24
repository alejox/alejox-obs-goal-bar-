#include "providers/chat_count_provider.hpp"

#include "core/poll_schedule.hpp"
#include "providers/provider_state_name.hpp"

#include <algorithm>
#include <utility>

namespace goalbar {

ChatCountProvider::ChatCountProvider(Platform platform, Metric metric, int min_chars, ChatSession session,
				     Logger log, std::chrono::milliseconds retry)
	: platform_(platform),
	  metric_(metric),
	  session_(std::move(session)),
	  log_(std::move(log)),
	  retry_(retry),
	  tally_(metric, min_chars)
{
	reading_.value = tally_.value();
	reading_.state = ProviderState::Connecting;
	worker_ = std::thread([this] { run(); });
}

ChatCountProvider::~ChatCountProvider()
{
	std::shared_ptr<ChatSocket> socket;
	{
		std::lock_guard lock(mutex_);
		stop_ = true;
		socket = socket_;
	}
	cancel_.cancel();
	if (socket)
		socket->close();
	wake_.notify_all();
	if (worker_.joinable())
		worker_.join();
}

MetricReading ChatCountProvider::latest() const
{
	std::lock_guard lock(mutex_);
	return reading_;
}

void ChatCountProvider::set_state(ProviderState state, const std::string &message)
{
	bool changed = false;
	{
		std::lock_guard lock(mutex_);
		changed = reading_.state != state || reading_.message != message;
		reading_.state = state;
		reading_.message = message;
		if (state == ProviderState::Live)
			reached_live_ = true;
	}
	if (changed && log_)
		log_(std::string(platform_id(platform_)) + " chat " + std::string(metric_id(metric_)) + ": " +
		     provider_state_name(state) + (message.empty() ? "" : " (" + message + ")"));
}

void ChatCountProvider::add(const ChatEvent &event)
{
	std::lock_guard lock(mutex_);
	tally_.add(event);
	reading_.value = tally_.value();
}

bool ChatCountProvider::stopping() const
{
	std::lock_guard lock(mutex_);
	return stop_;
}

bool ChatCountProvider::wait(std::chrono::milliseconds duration)
{
	std::unique_lock lock(mutex_);
	return wake_.wait_for(lock, duration, [this] { return stop_; });
}

void ChatCountProvider::attach(std::shared_ptr<ChatSocket> socket)
{
	bool stopped = false;
	{
		std::lock_guard lock(mutex_);
		stopped = stop_;
		socket_ = socket;
	}
	if (stopped && socket) // the destructor may already have looked for a socket
		socket->close();
}

void ChatCountProvider::run()
{
	unsigned failures = 0;
	while (!stopping()) {
		{
			std::lock_guard lock(mutex_);
			reached_live_ = false;
		}
		const std::string error = session_(*this);
		{
			std::lock_guard lock(mutex_);
			if (socket_)
				socket_->close();
			socket_.reset();
		}
		if (stopping())
			return;
		bool went_live = false;
		{
			std::lock_guard lock(mutex_);
			went_live = reached_live_;
		}
		if (error.empty()) {
			failures = 0;
		} else {
			failures = went_live ? 1 : failures + 1;
			set_state(ProviderState::Error, error);
		}
		// Reconnect after `retry_`, doubling on each failure in a row up to kMaxBackoff.
		std::chrono::milliseconds delay = retry_;
		for (unsigned i = 1; i < failures && delay < kMaxBackoff; ++i)
			delay *= 2;
		if (wait(std::min<std::chrono::milliseconds>(delay, kMaxBackoff)))
			return;
	}
}

} // namespace goalbar
