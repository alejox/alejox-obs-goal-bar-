// Network on macOS with NSURLSession (compiled with ARC).

#include "net/net.hpp"

#import <Foundation/Foundation.h>

namespace goalbar::net {

    const char *const kUserAgent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
                                   "(KHTML, like Gecko) Chrome/130.0 Safari/537.36";

    namespace {

        constexpr NSUInteger kMaxBody = 16 * 1024 * 1024;

        NSURLSession *session()
        {
            static NSURLSession *shared = [] {
                NSURLSessionConfiguration *config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
                config.HTTPAdditionalHeaders = @{@"User-Agent": @(kUserAgent)};
                config.HTTPCookieStorage = nil;
                config.URLCache = nil;
                return [NSURLSession sessionWithConfiguration:config];
            }();
            return shared;
        }

        std::string describe(NSError *error)
        {
            const char *text = error.localizedDescription.UTF8String;
            return text ? text : "network error";
        }

        Response perform(NSString *method, const std::string &url, const Headers &headers, const std::string *body,
                         int timeout_ms, CancelToken *cancel)
        {
            Response res;
            @autoreleasepool {
                NSURL *u = [NSURL URLWithString:@(url.c_str())];
                if (!u) {
                    res.error = "invalid URL: " + url;
                    return res;
                }
                NSMutableURLRequest *request =
                    [NSMutableURLRequest requestWithURL:u
                                            cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                        timeoutInterval:timeout_ms / 1000.0];
                request.HTTPMethod = method;
                for (const auto &[name, value] : headers)
                    [request setValue:@(value.c_str()) forHTTPHeaderField:@(name.c_str())];
                if (body) {
                    [request setValue:@"application/x-www-form-urlencoded" forHTTPHeaderField:@"Content-Type"];
                    request.HTTPBody = [NSData dataWithBytes:body->data() length:body->size()];
                }

                __block Response out;
                dispatch_semaphore_t done = dispatch_semaphore_create(0);
                NSURLSessionDataTask *task = [session()
                    dataTaskWithRequest:request
                      completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
                          if (error) {
                              out.error = describe(error);
                          } else if (data.length > kMaxBody) {
                              out.error = "response too large";
                          } else {
                              out.status = (int) ((NSHTTPURLResponse *) response).statusCode;
                              out.body.assign(static_cast<const char *>(data.bytes), data.length);
                          }
                          dispatch_semaphore_signal(done);
                      }];
                // -[NSURLSessionTask cancel] is thread-safe; the completion
                // handler then runs at once with NSURLErrorCancelled.
                AbortHook hook(cancel, [task] { [task cancel]; });
                if (!hook.armed()) {
                    res.error = kCancelledError;
                    return res;
                }
                [task resume];
                dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
                res = out;
                if (hook.disarm()) {
                    res = Response{};
                    res.error = kCancelledError;
                }
            }
            return res;
        }

    }  // namespace

    Response get(const std::string &url, const Headers &headers, int timeout_ms, CancelToken *cancel)
    {
        return perform(@"GET", url, headers, nullptr, timeout_ms, cancel);
    }

    Response post_form(const std::string &url, const std::string &body, const Headers &headers, int timeout_ms,
                       CancelToken *cancel)
    {
        return perform(@"POST", url, headers, &body, timeout_ms, cancel);
    }

}  // namespace goalbar::net
