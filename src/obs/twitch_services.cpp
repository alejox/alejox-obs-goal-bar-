#include "obs/twitch_services.hpp"

#include "net/net.hpp"
#include "providers/twitch_client_id.hpp"
#include "secrets/platform_secret_store.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <utility>

namespace goalbar::obs {
namespace {

HttpResult to_result(net::Response response)
{
	return HttpResult{response.status, std::move(response.body), std::move(response.error)};
}

TwitchHttp system_http()
{
	return TwitchHttp{
		[](const std::string &url, const HttpHeaders &headers, CancelToken &cancel) {
			return to_result(net::get(url, headers, 10000, &cancel));
		},
		[](const std::string &url, const std::string &body, CancelToken &cancel) {
			return to_result(net::post_form(url, body, {}, 10000, &cancel));
		}};
}

// Messages never carry tokens (see TwitchAccount).
void log_account(const std::string &message)
{
	obs_log(LOG_INFO, "%s", message.c_str());
}

} // namespace

std::shared_ptr<TwitchAccount> twitch_account()
{
	static const std::shared_ptr<TwitchAccount> account = std::make_shared<TwitchAccount>(
		kTwitchClientId, system_http(), make_platform_secret_store(), log_account);
	return account;
}

} // namespace goalbar::obs
