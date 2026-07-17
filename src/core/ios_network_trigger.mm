#include "ios_network_trigger.hpp"

#ifdef Q_OS_IOS
#import <Foundation/Foundation.h>

void triggerIOSNetworkPrompt()
{
    // Make a dummy URLSession request to force iOS to show the network permission dialog
    // on Chinese models if the app is freshly installed.
    NSURL *url = [NSURL URLWithString:@"http://captive.apple.com"];
    NSURLSession *session = [NSURLSession sharedSession];
    NSURLSessionDataTask *task = [session dataTaskWithURL:url completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
        // We do not care about the response. This is purely to trigger the prompt.
    }];
    [task resume];
}
#endif
