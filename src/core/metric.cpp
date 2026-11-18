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
	case Metric::Bits:
		return "bits";
	case Metric::Comments:
		return "comments";
	case Metric::PaidSubs:
		return "paid_subs";
	}
	return "likes";
}

std::optional<Metric> metric_from_id(std::string_view id)
{
	for (Metric metric : {Metric::Likes, Metric::Subscribers, Metric::Followers, Metric::Viewers, Metric::Bits,
			      Metric::Comments, Metric::PaidSubs}) {
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

bool metric_is_counted(Metric metric)
{
	return metric == Metric::Bits || metric == Metric::Comments || metric == Metric::PaidSubs;
}

bool platform_reads(Platform platform, Metric metric)
{
	switch (platform) {
	case Platform::YouTube:
		return metric == Metric::Likes || metric == Metric::Subscribers || metric == Metric::Comments ||
		       metric == Metric::PaidSubs;
	case Platform::Twitch:
		return metric == Metric::Followers || metric == Metric::Subscribers || metric == Metric::Bits ||
		       metric == Metric::Comments;
	case Platform::Kick:
		return metric == Metric::Followers || metric == Metric::Viewers || metric == Metric::Comments ||
		       metric == Metric::PaidSubs;
	case Platform::Manual:
		return metric != Metric::Viewers && !metric_is_counted(metric);
	}
	return false;
}

Metric readable_metric(Platform platform, Metric metric)
{
	if (platform_reads(platform, metric))
		return metric;
	switch (platform) {
	case Platform::Twitch:
	case Platform::Kick:
		return Metric::Followers;
	default:
		return Metric::Likes;
	}
}

} // namespace goalbar
