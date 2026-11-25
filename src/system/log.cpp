#include "system/log.hpp"

#include <mutex>
#include <utility>

namespace goalbar::sys {
namespace {

std::mutex &sink_mutex()
{
	static std::mutex mutex;
	return mutex;
}

LogSink &sink()
{
	static LogSink value;
	return value;
}

} // namespace

void set_log_sink(LogSink new_sink)
{
	std::lock_guard lock(sink_mutex());
	sink() = std::move(new_sink);
}

void log(const std::string &message)
{
	LogSink copy;
	{
		std::lock_guard lock(sink_mutex());
		copy = sink();
	}
	if (copy)
		copy(message);
}

} // namespace goalbar::sys
