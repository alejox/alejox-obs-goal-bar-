#include "obs/kick_services.hpp"

#include "net/net.hpp"
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

KickHttp system_http()
{
	return KickHttp{[](const std::string &url, const HttpHeaders &headers, CancelToken &cancel) {
				return to_result(net::get(url, headers, 10000, &cancel));
			},
			[](const std::string &url, const std::string &body, CancelToken &cancel) {
				return to_result(net::post_form(url, body, {}, 10000, &cancel));
			}};
}

// Messages never carry the client secret or tokens (see KickAccount).
void log_account(const std::string &message)
{
	obs_log(LOG_INFO, "%s", message.c_str());
}

} // namespace

// The network layer does not report response headers yet, so Retry-After
// stays unset and 429s fall back to the exponential backoff.
std::shared_ptr<KickAccount> kick_account()
{
	static const std::shared_ptr<KickAccount> account =
		std::make_shared<KickAccount>(system_http(), make_platform_secret_store(), log_account);
	return account;
}

} // namespace goalbar::obs
