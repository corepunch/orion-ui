#include "audio_input.h"
#import <TargetConditionals.h>
#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>

struct ui_audio_input {
  AudioQueueRef queue;
  void (*receive)(void *, const int16_t *, int);
  void *userdata;
#if TARGET_OS_IPHONE
  NSString *category, *mode;
  AVAudioSessionCategoryOptions options;
#endif
};
static atomic_int microphone_request_pending;

ui_microphone_permission_t ui_microphone_permission(bool request) {
  AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
  if (status == AVAuthorizationStatusAuthorized) return UI_MIC_ALLOWED;
  if (status == AVAuthorizationStatusDenied || status == AVAuthorizationStatusRestricted) return UI_MIC_DENIED;
  if (request && !atomic_exchange(&microphone_request_pending, 1))
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
      atomic_store(&microphone_request_pending, 0);
    }];
  return atomic_load(&microphone_request_pending) ? UI_MIC_PENDING : UI_MIC_UNKNOWN;
}

static void microphone_receive(void *userdata, AudioQueueRef queue, AudioQueueBufferRef buffer,
                               const AudioTimeStamp *time, UInt32 packets, const AudioStreamPacketDescription *descriptions) {
  ui_audio_input_t *input = userdata;
  input->receive(input->userdata, buffer->mAudioData, (int)(buffer->mAudioDataByteSize / sizeof(int16_t)));
  AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
}

void ui_audio_input_close(ui_audio_input_t *input) {
  if (!input) return;
  if (input->queue) { AudioQueueStop(input->queue, true); AudioQueueDispose(input->queue, true); }
#if TARGET_OS_IPHONE
  if (input->category) {
    NSError *error = nil;
    if (![[AVAudioSession sharedInstance] setCategory:input->category mode:input->mode options:input->options error:&error])
      fprintf(stderr, "[microphone] restore audio session failed: %s\n", error.localizedDescription.UTF8String);
    [input->category release]; [input->mode release];
  }
#endif
  free(input);
}

ui_audio_input_t *ui_audio_input_open(int rate, void (*receive)(void *, const int16_t *, int), void *userdata) {
  if (rate < 8000 || rate > 192000 || !receive || ui_microphone_permission(false) != UI_MIC_ALLOWED) {
    fprintf(stderr, "[microphone] input rejected rate=%d callback=%d permission=%d\n", rate, receive != NULL, ui_microphone_permission(false));
    fflush(stderr); return NULL;
  }
  ui_audio_input_t *input = calloc(1, sizeof(*input));
  if (!input) { fprintf(stderr, "[microphone] input allocation failed\n"); fflush(stderr); return NULL; }
  input->receive = receive; input->userdata = userdata;
#if TARGET_OS_IPHONE
  AVAudioSession *session = [AVAudioSession sharedInstance];
  input->category = [session.category copy]; input->mode = [session.mode copy]; input->options = session.categoryOptions;
  NSError *error = nil;
  if (![session setCategory:AVAudioSessionCategoryPlayAndRecord mode:AVAudioSessionModeDefault
                   options:AVAudioSessionCategoryOptionDefaultToSpeaker error:&error] || ![session setActive:YES error:&error]) {
    fprintf(stderr, "[microphone] audio session failed: %s\n", error.localizedDescription.UTF8String);
    ui_audio_input_close(input); return NULL;
  }
#endif
  AudioStreamBasicDescription format = {0};
  format.mSampleRate = rate; format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
  format.mChannelsPerFrame = 1; format.mBitsPerChannel = 16;
  format.mFramesPerPacket = 1; format.mBytesPerFrame = format.mBytesPerPacket = 2;
  OSStatus status = AudioQueueNewInput(&format, microphone_receive, input, NULL, NULL, 0, &input->queue);
  for (int i = 0; status == noErr && i < 3; i++) {
    AudioQueueBufferRef buffer;
    status = AudioQueueAllocateBuffer(input->queue, 2048, &buffer);
    if (status == noErr) status = AudioQueueEnqueueBuffer(input->queue, buffer, 0, NULL);
  }
  if (status == noErr) status = AudioQueueStart(input->queue, NULL);
  if (status != noErr) {
    fprintf(stderr, "[microphone] input open failed status=%d\n", (int)status);
    ui_audio_input_close(input); return NULL;
  }
  return input;
}
