#include "obs/provider_factory.hpp"

#include "net/net.hpp"
#include "obs/kick_services.hpp"
#include "obs/twitch_services.hpp"
#include "providers/kick_provider.hpp"
#include "providers/twitch_provider.hpp"
#include "providers/youtube_public_provider.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <utility>

namespace goalbar::obs {
namespace {

// The consent cookies skip the EU cookie wall; English keeps the subscriber
// text in the format the parser reads.
const net::Headers kYoutubeHeaders = {{"Cookie", "CONSENT=YES+1; SOCS=CAI"},
				      {"Accept-Language", "en-US,en;q=0.9"}};

HttpResult fetch_youtube_page(const std::string &url, CancelToken &cancel)
{
	net::Response response = net::get(url, kYoutubeHeaders, 10000, &cancel);
	return HttpResult{response.status, std::move(response.body), std::move(response.error)};
}

void log_provider(const std::string &message)
{
	obs_log(LOG_INFO, "%s", message.c_str());
}

} // namespace

std::unique_ptr<MetricProvider> make_youtube_provider(const std::string &channel_url, Metric metric)
{
	return std::make_unique<YoutubePublicProvider>(channel_url, metric, fetch_youtube_page, log_provider);
}

std::unique_ptr<MetricProvider> make_twitch_provider(Metric metric)
{
	return std::make_unique<TwitchProvider>(twitch_account(), metric, log_provider);
}

std::unique_ptr<MetricProvider> make_kick_provider(const std::string &slug, Metric metric)
{
	return std::make_unique<KickProvider>(kick_account(), slug, metric, log_provider);
}

} // namespace goalbar::obs
