#include "media_export.h"
#import <TargetConditionals.h>
#import <AVFoundation/AVFoundation.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool media_apple_progress(ui_media_progress_fn progress, void *userdata, float n) {
  return !progress || progress(userdata, n);
}

static AVAssetTrack *media_apple_track(AVAsset *asset, AVMediaType type, ui_media_progress_fn progress, void *userdata) {
  __block AVAssetTrack *track = nil;
  dispatch_semaphore_t done = dispatch_semaphore_create(0);
  [asset loadTracksWithMediaType:type completionHandler:^(NSArray<AVAssetTrack *> *tracks, NSError *error) {
    track = [tracks.firstObject retain];
    if (error) { fprintf(stderr, "[media] loading %s track failed: %s\n", type.UTF8String, error.localizedDescription.UTF8String); fflush(stderr); }
    dispatch_semaphore_signal(done);
  }];
  bool cancelled = false;
  while (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 20000000))) {
    if (!cancelled && !media_apple_progress(progress, userdata, .8f)) { cancelled = true; [asset cancelLoading]; }
  }
  dispatch_release(done);
  if (cancelled) { [track release]; return nil; }
  return [track autorelease];
}

static bool media_apple_movie(NSURL *url, const ui_media_video_t *v, ui_media_progress_fn progress, void *userdata) {
  NSError *error = nil;
  AVAssetWriter *writer = [[AVAssetWriter alloc] initWithURL:url fileType:AVFileTypeMPEG4 error:&error];
  NSDictionary *settings = @{AVVideoCodecKey: AVVideoCodecTypeH264, AVVideoWidthKey: @(v->width), AVVideoHeightKey: @(v->height),
    AVVideoCompressionPropertiesKey: @{AVVideoAverageBitRateKey: @4000000, AVVideoMaxKeyFrameIntervalKey: @(v->fps * 2)}};
  AVAssetWriterInput *input = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:settings];
  input.expectsMediaDataInRealTime = NO;
  AVAssetWriterInputPixelBufferAdaptor *adaptor = [AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:input
    sourcePixelBufferAttributes:@{(id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
      (id)kCVPixelBufferWidthKey: @(v->width), (id)kCVPixelBufferHeightKey: @(v->height), (id)kCVPixelBufferIOSurfacePropertiesKey: @{}}];
  bool ok = writer && [writer canAddInput:input];
  if (ok) { [writer addInput:input]; ok = [writer startWriting]; }
  if (ok) [writer startSessionAtSourceTime:kCMTimeZero];
  uint8_t *rgba = malloc((size_t)v->width * v->height * 4);
  ok = ok && rgba;
  int64_t count = (v->frames * v->fps + v->rate - 1) / v->rate;
  for (int64_t i = 0; ok && i < count; i++) {
    @autoreleasepool {
      while (ok && !input.readyForMoreMediaData) {
        ok = writer.status == AVAssetWriterStatusWriting && media_apple_progress(progress, userdata, (float)i / count * .8f);
        if (ok) usleep(2000);
      }
      ok = ok && media_apple_progress(progress, userdata, (float)i / count * .8f) && ui_media_video_frame(v, i, rgba);
      CVPixelBufferRef buffer = NULL;
      if (ok) ok = adaptor.pixelBufferPool && CVPixelBufferPoolCreatePixelBuffer(NULL, adaptor.pixelBufferPool, &buffer) == kCVReturnSuccess;
      if (ok) {
        CVPixelBufferLockBaseAddress(buffer, 0);
        uint8_t *base = CVPixelBufferGetBaseAddress(buffer);
        size_t stride = CVPixelBufferGetBytesPerRow(buffer);
        for (int y = 0; y < v->height; y++) for (int x = 0; x < v->width; x++) {
          const uint8_t *src = rgba + ((size_t)y * v->width + x) * 4;
          uint8_t *dst = base + (size_t)y * stride + x * 4;
          dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = 255;
        }
        CVPixelBufferUnlockBaseAddress(buffer, 0);
        ok = [adaptor appendPixelBuffer:buffer withPresentationTime:CMTimeMake(i, v->fps)];
      }
      if (buffer) CVPixelBufferRelease(buffer);
    }
  }
  free(rgba);
  if (ok) {
    [input markAsFinished];
    [writer endSessionAtSourceTime:CMTimeMake(v->frames, v->rate)];
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [writer finishWritingWithCompletionHandler:^{ dispatch_semaphore_signal(done); }];
    while (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 20000000))) {
      if (!media_apple_progress(progress, userdata, .8f)) [writer cancelWriting];
    }
    dispatch_release(done);
    ok = writer.status == AVAssetWriterStatusCompleted;
  } else [writer cancelWriting];
  if (!ok) { fprintf(stderr, "[media] native H.264 failed: %s\n", (writer.error ?: error).localizedDescription.UTF8String ?: "cancelled"); fflush(stderr); }
  [writer release];
  return ok;
}

bool ui_media_encode(const char *wav, const char *output, ui_media_format_t format,
                     const ui_media_video_t *video, ui_media_progress_fn progress, void *userdata) {
  @autoreleasepool {
    NSURL *outURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:output]];
    AVURLAsset *audio = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:wav]] options:nil];
    AVAsset *source = audio;
    NSURL *silentURL = [NSURL fileURLWithPath:[[NSString stringWithUTF8String:output] stringByAppendingString:@".silent.mp4"]];
    bool ok = format == UI_MEDIA_M4A || (format == UI_MEDIA_MP4 && video && media_apple_movie(silentURL, video, progress, userdata));
    if (ok && format == UI_MEDIA_MP4) {
      AVURLAsset *silent = [AVURLAsset URLAssetWithURL:silentURL options:nil];
      AVMutableComposition *composition = [AVMutableComposition composition];
      AVAssetTrack *a = media_apple_track(audio, AVMediaTypeAudio, progress, userdata);
      AVAssetTrack *v = a ? media_apple_track(silent, AVMediaTypeVideo, progress, userdata) : nil;
      CMTimeRange range = CMTimeRangeMake(kCMTimeZero, CMTimeMake(video->frames, video->rate));
      NSError *error = nil;
      ok = a && v && [[composition addMutableTrackWithMediaType:AVMediaTypeAudio preferredTrackID:kCMPersistentTrackID_Invalid]
        insertTimeRange:range ofTrack:a atTime:kCMTimeZero error:&error] &&
        [[composition addMutableTrackWithMediaType:AVMediaTypeVideo preferredTrackID:kCMPersistentTrackID_Invalid]
        insertTimeRange:range ofTrack:v atTime:kCMTimeZero error:&error];
      source = composition;
    }
    AVAssetExportSession *session = nil;
    if (ok) {
      session = [[AVAssetExportSession alloc] initWithAsset:source presetName:format == UI_MEDIA_M4A ? AVAssetExportPresetAppleM4A : AVAssetExportPresetHighestQuality];
      session.outputURL = outURL;
      session.outputFileType = format == UI_MEDIA_M4A ? AVFileTypeAppleM4A : AVFileTypeMPEG4;
      session.shouldOptimizeForNetworkUse = YES;
      ok = session != nil;
    }
    if (ok) {
      dispatch_semaphore_t done = dispatch_semaphore_create(0);
      [session exportAsynchronouslyWithCompletionHandler:^{ dispatch_semaphore_signal(done); }];
      while (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 20000000))) {
        float fraction = format == UI_MEDIA_MP4 ? .8f + session.progress * .2f : session.progress;
        if (!media_apple_progress(progress, userdata, fraction)) [session cancelExport];
      }
      dispatch_release(done);
      ok = session.status == AVAssetExportSessionStatusCompleted;
    }
    if (!ok) { fprintf(stderr, "[media] native export failed: %s\n", session.error.localizedDescription.UTF8String ?: "cancelled or unavailable"); fflush(stderr); }
    [session release];
    [[NSFileManager defaultManager] removeItemAtURL:silentURL error:nil];
    if (!ok) [[NSFileManager defaultManager] removeItemAtURL:outURL error:nil];
    return ok;
  }
}

bool ui_media_share(const char *path) {
  @autoreleasepool {
    NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
    if (![[NSFileManager defaultManager] fileExistsAtPath:url.path]) return false;
#if TARGET_OS_IPHONE
    UIWindow *window = nil;
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) if ([scene isKindOfClass:[UIWindowScene class]])
      for (UIWindow *candidate in ((UIWindowScene *)scene).windows) if (candidate.isKeyWindow) window = candidate;
    UIViewController *presenter = window.rootViewController;
    while (presenter.presentedViewController) presenter = presenter.presentedViewController;
    if (!presenter) return false;
    UIActivityViewController *share = [[UIActivityViewController alloc] initWithActivityItems:@[url] applicationActivities:nil];
    share.popoverPresentationController.sourceView = presenter.view;
    share.popoverPresentationController.sourceRect = CGRectMake(presenter.view.bounds.size.width / 2, presenter.view.bounds.size.height / 2, 1, 1);
    share.popoverPresentationController.permittedArrowDirections = 0;
    [presenter presentViewController:share animated:YES completion:nil];
    [share release];
#else
    NSView *view = NSApp.keyWindow.contentView ?: NSApp.mainWindow.contentView;
    if (!view) return false;
    NSSharingServicePicker *picker = [[NSSharingServicePicker alloc] initWithItems:@[url]];
    [picker showRelativeToRect:NSMakeRect(NSMidX(view.bounds), NSMidY(view.bounds), 1, 1) ofView:view preferredEdge:NSMinYEdge];
    [picker autorelease];
#endif
    return true;
  }
}
