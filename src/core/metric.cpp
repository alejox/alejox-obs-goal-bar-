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
	case Metric::Viewers:
		return "viewers";
	}
	return "likes";
}

std::optional<Metric> metric_from_id(std::string_view id)
{
	for (Metric metric : {Metric::Likes, Metric::Subscribers, Metric::Followers, Metric::Viewers}) {
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
	case Platform::Kick:
		return "kick";
	}
	return "manual";
}

std::optional<Platform> platform_from_id(std::string_view id)
{
	for (Platform platform : {Platform::Manual, Platform::YouTube, Platform::Twitch, Platform::Kick}) {
		if (platform_id(platform) == id)
			return platform;
	}
	return std::nullopt;
}

} // namespace goalbar
