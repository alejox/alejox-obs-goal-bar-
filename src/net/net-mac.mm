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

    }  // namespace

    Response get(const std::string &url, const Headers &headers, int timeout_ms)
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
            for (const auto &[name, value] : headers)
                [request setValue:@(value.c_str()) forHTTPHeaderField:@(name.c_str())];

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
            [task resume];
            dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
            res = out;
        }
        return res;
    }

}  // namespace goalbar::net
