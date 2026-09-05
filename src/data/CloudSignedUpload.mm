#import <Foundation/Foundation.h>

#include "CloudSignedUpload.h"

CloudSignedUploadResult uploadFileToSignedUrl(const juce::String& urlText,
                                              const juce::File& file,
                                              const juce::String& contentType,
                                              const int timeoutMs) {
  CloudSignedUploadResult result;
  @autoreleasepool {
    const auto urlString = urlText.toStdString();
    const auto path = file.getFullPathName().toStdString();
    NSURL* url = [NSURL URLWithString:[NSString stringWithUTF8String:urlString.c_str()]];
    NSURL* sourceFile = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    if (url == nil || sourceFile == nil) {
      result.errorMessage = "Invalid signed upload URL or local file.";
      return result;
    }
    NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url];
    request.HTTPMethod = @"PUT";
    request.timeoutInterval = juce::jmax(1, timeoutMs) / 1000.0;
    [request setValue:[NSString stringWithUTF8String:contentType.toRawUTF8()]
       forHTTPHeaderField:@"Content-Type"];

    dispatch_semaphore_t completed = dispatch_semaphore_create(0);
    __block NSInteger status = 0;
    __block juce::String responseBody;
    __block juce::String transferErrorMessage;
    NSURLSessionConfiguration* configuration = [NSURLSessionConfiguration ephemeralSessionConfiguration];
    configuration.timeoutIntervalForRequest = request.timeoutInterval;
    NSURLSession* session = [NSURLSession sessionWithConfiguration:configuration];
    NSURLSessionUploadTask* task = [session uploadTaskWithRequest:request
                                                           fromFile:sourceFile
                                                  completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
      status = [(NSHTTPURLResponse*) response statusCode];
      // This target does not use ARC. Copy Foundation values while they are
      // owned by NSURLSession instead of retaining unsafe __block pointers.
      if (data != nil)
        responseBody = juce::String::fromUTF8(
            static_cast<const char*>(data.bytes), static_cast<int>(data.length));
      if (error != nil)
        transferErrorMessage = juce::String::fromUTF8(
            error.localizedDescription.UTF8String);
      dispatch_semaphore_signal(completed);
    }];
    [task resume];
    const auto wait = dispatch_semaphore_wait(
        completed, dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(timeoutMs + 5000) * NSEC_PER_MSEC));
    if (wait != 0) {
      [task cancel];
      result.errorMessage = "Signed upload timed out.";
      [session invalidateAndCancel];
      return result;
    }
    [session finishTasksAndInvalidate];
    result.httpCode = static_cast<int>(status);
    result.responseBody = std::move(responseBody);
    if (transferErrorMessage.isNotEmpty()) {
      result.errorMessage = std::move(transferErrorMessage);
      return result;
    }
    result.ok = status >= 200 && status < 300;
    if (!result.ok && result.errorMessage.isEmpty())
      result.errorMessage = "Signed upload failed.";
  }
  return result;
}
