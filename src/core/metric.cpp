#include "core/metric.hpp"

namespace goalbar {

std::string_view metric_id(Metric metric)
{
	switch (metric) {
	case Metric::Likes:
		return "likes";
	case Metric::Subscribers:
		return "subscribers";
	case Metric::Followers:
		return "followers";
	}
	return "likes";
}

std::optional<Metric> metric_from_id(std::string_view id)
{
	for (Metric metric : {Metric::Likes, Metric::Subscribers, Metric::Followers}) {
		if (metric_id(metric) == id)
			return metric;
	}
	return std::nullopt;
}

std::string_view platform_id(Platform platform)
{
	switch (platform) {
	case Platform::Manual:
		return "manual";
	case Platform::YouTube:
		return "youtube";
	case Platform::Twitch:
		return "twitch";
	}
	return "manual";
}

std::optional<Platform> platform_from_id(std::string_view id)
{
	for (Platform platform : {Platform::Manual, Platform::YouTube, Platform::Twitch}) {
		if (platform_id(platform) == id)
			return platform;
	}
	return std::nullopt;
}

} // namespace goalbar
