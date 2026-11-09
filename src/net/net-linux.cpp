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

} // namespace

Response get(const std::string &url, const Headers &headers, int timeout_ms)
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
	const CURLcode rc = curl_easy_perform(h);
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
