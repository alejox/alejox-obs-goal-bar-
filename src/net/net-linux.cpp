// Network on Linux with libcurl (OBS already depends on it).

#include "net/net.hpp"

#include <curl/curl.h>

#include <mutex>

namespace goalbar::net {

const char *const kUserAgent = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
			       "(KHTML, like Gecko) Chrome/130.0 Safari/537.36";

namespace {

constexpr size_t kMaxBody = 16 * 1024 * 1024;

void global_init()
{
	static std::once_flag once;
	std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

struct Easy {
	CURL *h = curl_easy_init();
	curl_slist *headers = nullptr;
	Easy() = default;
	Easy(const Easy &) = delete;
	Easy &operator=(const Easy &) = delete;
	~Easy()
	{
		if (h)
			curl_easy_cleanup(h);
		curl_slist_free_all(headers);
	}
};

struct Transfer {
	Response *res;
	bool too_big = false;
};

size_t on_body(char *data, size_t size, size_t count, void *user)
{
	auto *transfer = static_cast<Transfer *>(user);
	const size_t n = size * count;
	if (transfer->res->body.size() + n > kMaxBody) {
		transfer->too_big = true;
		return 0; // aborts the transfer
	}
	transfer->res->body.append(data, n);
	return n;
}

// Runs the transfer on a multi handle so a cancel wakes the wait at once
// (curl_multi_wakeup is the one libcurl call that is safe from another thread).
// Needs libcurl 7.68+.
CURLcode perform(CURL *easy, CancelToken *cancel, bool &cancelled)
{
	cancelled = false;
	if (!cancel)
		return curl_easy_perform(easy);
	CURLM *multi = curl_multi_init();
	if (!multi)
		return CURLE_OUT_OF_MEMORY;
	curl_multi_add_handle(multi, easy);

	CURLcode result = CURLE_OK;
	AbortHook hook(cancel, [multi] { curl_multi_wakeup(multi); });
	cancelled = !hook.armed();
	for (int running = 1; running && !cancelled;) {
		if (curl_multi_perform(multi, &running) != CURLM_OK) {
			result = CURLE_FAILED_INIT;
			break;
		}
		if (!running) {
			int queued = 0;
			const CURLMsg *message = curl_multi_info_read(multi, &queued);
			result = message && message->msg == CURLMSG_DONE ? message->data.result : CURLE_OK;
			break;
		}
		curl_multi_poll(multi, nullptr, 0, 1000, nullptr);
		cancelled = cancel->cancelled();
	}
	// Disarm before the multi handle goes away: the hook must never wake a freed handle.
	cancelled = hook.disarm() || cancelled;
	curl_multi_remove_handle(multi, easy);
	curl_multi_cleanup(multi);
	return result;
}

} // namespace

Response get(const std::string &url, const Headers &headers, int timeout_ms, CancelToken *cancel)
{
	global_init();
	Response res;
	Easy easy;
	if (!easy.h) {
		res.error = "curl_easy_init failed";
		return res;
	}
	CURL *h = easy.h;
	curl_easy_setopt(h, CURLOPT_URL, url.c_str());
	curl_easy_setopt(h, CURLOPT_USERAGENT, kUserAgent);
	curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, long(timeout_ms));
	curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, long(timeout_ms));
	curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(h, CURLOPT_MAXREDIRS, 10L);
	curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, ""); // whatever this libcurl supports
	curl_easy_setopt(h, CURLOPT_HTTPGET, 1L);
	for (const auto &[name, value] : headers)
		easy.headers = curl_slist_append(easy.headers, (name + ": " + value).c_str());
	if (easy.headers)
		curl_easy_setopt(h, CURLOPT_HTTPHEADER, easy.headers);
	Transfer transfer{&res};
	curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, on_body);
	curl_easy_setopt(h, CURLOPT_WRITEDATA, &transfer);
	bool cancelled = false;
	const CURLcode rc = perform(h, cancel, cancelled);
	if (cancelled) {
		res.body.clear();
		res.error = kCancelledError;
		res.status = 0;
		return res;
	}
	if (rc != CURLE_OK) {
		res.error = transfer.too_big ? "response too large" : curl_easy_strerror(rc);
		res.status = 0;
		return res;
	}
	long status = 0;
	curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
	res.status = int(status);
	return res;
}

} // namespace goalbar::net
