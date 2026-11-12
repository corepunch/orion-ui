#import <Foundation/Foundation.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static NSString *share_base(void) {
  NSString *base = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"GrooveShareBaseURL"];
  if (![base isKindOfClass:[NSString class]]) return nil;
  NSURL *url = [NSURL URLWithString:base];
  if (![url.scheme.lowercaseString isEqualToString:@"https"] || !url.host.length || url.user || url.password ||
      url.query || url.fragment || (url.path.length && ![url.path isEqualToString:@"/"])) return nil;
  return [base stringByTrimmingCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@"/"]];
}

bool groove_share_configured(void) { @autoreleasepool { return share_base() != nil; } }

bool groove_share_temp_dir(char *path, size_t cap) {
  @autoreleasepool {
    NSString *folder = [NSTemporaryDirectory() stringByAppendingPathComponent:[[NSUUID UUID] UUIDString]];
    return [folder getFileSystemRepresentation:path maxLength:cap] &&
      [[NSFileManager defaultManager] createDirectoryAtPath:folder withIntermediateDirectories:NO attributes:nil error:NULL];
  }
}

static NSData *share_request(NSURLRequest *request, NSInteger *status) {
  NSURLSessionConfiguration *config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
  config.timeoutIntervalForResource = 120;
  NSURLSession *session = [NSURLSession sessionWithConfiguration:config];
  dispatch_semaphore_t done = dispatch_semaphore_create(0);
  __block NSData *body = nil;
  [[session dataTaskWithRequest:request completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
    if (!error && [response isKindOfClass:[NSHTTPURLResponse class]]) {
      *status = ((NSHTTPURLResponse *)response).statusCode;
      body = [data retain];
    }
    dispatch_semaphore_signal(done);
  }] resume];
  dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
  dispatch_release(done);
  [session finishTasksAndInvalidate];
  return [body autorelease];
}

bool groove_share_upload(const char *title, const char *audio, const char *project, char *url, size_t cap) {
  @autoreleasepool {
    NSString *base = share_base();
    NSString *name = [NSString stringWithUTF8String:title];
    NSData *mp3 = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:audio]];
    NSData *song = project ? [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:project]] : nil;
    if (!base || !name.length || !mp3.length || mp3.length > 12 * 1024 * 1024 || (project && (!song.length || song.length > 20 * 1024 * 1024))) return false;
    NSString *boundary = [[NSUUID UUID] UUIDString];
    NSMutableData *body = [NSMutableData data];
#define APPEND_TEXT(s) [body appendData:[(s) dataUsingEncoding:NSUTF8StringEncoding]]
    APPEND_TEXT(([NSString stringWithFormat:@"--%@\r\nContent-Disposition: form-data; name=\"title\"\r\n\r\n%@\r\n", boundary, name]));
    APPEND_TEXT(([NSString stringWithFormat:@"--%@\r\nContent-Disposition: form-data; name=\"audio\"; filename=\"song.mp3\"\r\nContent-Type: audio/mpeg\r\n\r\n", boundary]));
    [body appendData:mp3]; APPEND_TEXT(@"\r\n");
    if (song) {
      APPEND_TEXT(([NSString stringWithFormat:@"--%@\r\nContent-Disposition: form-data; name=\"project\"; filename=\"song.groove\"\r\nContent-Type: application/octet-stream\r\n\r\n", boundary]));
      [body appendData:song]; APPEND_TEXT(@"\r\n");
    }
    APPEND_TEXT(([NSString stringWithFormat:@"--%@--\r\n", boundary]));
#undef APPEND_TEXT
    if (body.length > 33 * 1024 * 1024) return false;
    NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:[base stringByAppendingString:@"/api/songs"]]];
    request.HTTPMethod = @"POST"; request.HTTPBody = body; request.timeoutInterval = 120;
    [request setValue:[@"multipart/form-data; boundary=" stringByAppendingString:boundary] forHTTPHeaderField:@"Content-Type"];
    NSInteger status = 0;
    NSData *responseData = share_request(request, &status);
    NSDictionary *json = responseData ? [NSJSONSerialization JSONObjectWithData:responseData options:0 error:NULL] : nil;
    NSString *link = [json isKindOfClass:[NSDictionary class]] ? json[@"url"] : nil;
    NSString *identifier = [json isKindOfClass:[NSDictionary class]] ? json[@"id"] : nil;
    NSString *token = [json isKindOfClass:[NSDictionary class]] ? json[@"deleteToken"] : nil;
    bool ok = status == 201 && [identifier isKindOfClass:[NSString class]] && identifier.length == 22 &&
      [token isKindOfClass:[NSString class]] && token.length == 43 &&
      [link isKindOfClass:[NSString class]] && [link isEqualToString:[NSString stringWithFormat:@"%@/s/%@", base, identifier]] && strlen(link.UTF8String) < cap;
    if (ok) {
      NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
      NSMutableArray *receipts = [NSMutableArray arrayWithArray:[defaults arrayForKey:@"GrooveSharedSongs"] ?: @[]];
      [receipts addObject:@{@"id": identifier, @"url": link, @"deleteToken": token, @"baseURL": base, @"title": name}];
      [defaults setObject:receipts forKey:@"GrooveSharedSongs"];
      snprintf(url, cap, "%s", link.UTF8String);
    }
    return ok;
  }
}

size_t groove_share_count(void) {
  @autoreleasepool { return [[[NSUserDefaults standardUserDefaults] arrayForKey:@"GrooveSharedSongs"] count]; }
}

bool groove_share_at(size_t index, char *title, size_t title_cap, char *url, size_t url_cap) {
  @autoreleasepool {
    NSArray *receipts = [[NSUserDefaults standardUserDefaults] arrayForKey:@"GrooveSharedSongs"];
    if (index >= receipts.count) return false;
    NSDictionary *receipt = receipts[index];
    NSString *name = receipt[@"title"], *link = receipt[@"url"];
    if (![name isKindOfClass:[NSString class]] || ![link isKindOfClass:[NSString class]] || strlen(name.UTF8String) >= title_cap || strlen(link.UTF8String) >= url_cap) return false;
    snprintf(title, title_cap, "%s", name.UTF8String); snprintf(url, url_cap, "%s", link.UTF8String);
    return true;
  }
}

bool groove_share_delete(size_t index) {
  @autoreleasepool {
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSMutableArray *receipts = [NSMutableArray arrayWithArray:[defaults arrayForKey:@"GrooveSharedSongs"] ?: @[]];
    if (index >= receipts.count) return false;
    NSDictionary *receipt = receipts[index];
    NSString *base = receipt[@"baseURL"], *identifier = receipt[@"id"], *token = receipt[@"deleteToken"];
    if (![base isKindOfClass:[NSString class]] || ![identifier isKindOfClass:[NSString class]] || ![token isKindOfClass:[NSString class]]) return false;
    NSString *escaped = [identifier stringByAddingPercentEncodingWithAllowedCharacters:[NSCharacterSet alphanumericCharacterSet]];
    NSURL *endpoint = [NSURL URLWithString:[NSString stringWithFormat:@"%@/api/songs/%@", base, escaped]];
    if (!endpoint || ![endpoint.scheme.lowercaseString isEqualToString:@"https"]) return false;
    NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:endpoint];
    request.HTTPMethod = @"DELETE"; request.timeoutInterval = 120;
    [request setValue:[@"Bearer " stringByAppendingString:token] forHTTPHeaderField:@"Authorization"];
    NSInteger status = 0;
    share_request(request, &status);
    if (status != 204 && status != 404) return false;
    [receipts removeObjectAtIndex:index];
    [defaults setObject:receipts forKey:@"GrooveSharedSongs"];
    return true;
  }
}
