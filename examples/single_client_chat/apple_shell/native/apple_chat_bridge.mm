#import "apple_chat_bridge.h"

#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "apple_chat_runtime.h"

@interface ATAppleChatBridge ()
- (void)publishUID:(std::string const &)uid;
- (void)publishTranscript:(std::string const &)transcript;
@end

@implementation ATAppleChatBridge {
  std::unique_ptr<apptraverse::apple::AppleChatRuntime> _runtime;
  std::thread _thread;
  BOOL _started;
  void (^_onChange)(void);
}

- (instancetype)initWithStateDirectory:(NSString *)stateDirectory
                            clientName:(NSString *)clientName
                       localClientName:(NSString *)localClientName {
  self = [super init];
  if (self == nil) {
    return nil;
  }

  _localUID = @"";
  _timelineRows = @[];
  _started = NO;

  std::string const state_dir = stateDirectory.UTF8String ? stateDirectory.UTF8String : "";
  std::string const aether_name = clientName.UTF8String ? clientName.UTF8String : "apptraverse-apple";
  std::string const local_name =
      localClientName.UTF8String ? localClientName.UTF8String : "Apple";

  __weak ATAppleChatBridge *weakSelf = self;
  apptraverse::apple::AppleChatRuntime::UiCallbacks callbacks;
  callbacks.on_aether_uid = [weakSelf](std::string uid) {
    ATAppleChatBridge *strongSelf = weakSelf;
    if (strongSelf == nil) {
      return;
    }
    [strongSelf publishUID:uid];
  };
  callbacks.on_transcript = [weakSelf](std::string text) {
    ATAppleChatBridge *strongSelf = weakSelf;
    if (strongSelf == nil) {
      return;
    }
    [strongSelf publishTranscript:text];
  };

  _runtime = std::make_unique<apptraverse::apple::AppleChatRuntime>(
      state_dir, aether_name, local_name, std::move(callbacks));
  return self;
}

- (void)dealloc {
  [self stop];
}

- (void)setOnChange:(void (^)(void))block {
  _onChange = [block copy];
}

- (void)start {
  if (_runtime == nullptr || _started) {
    return;
  }
  _started = YES;
  _thread = std::thread([self]() { self->_runtime->Run(); });
}

- (void)stop {
  if (_runtime != nullptr) {
    _runtime->Stop();
  }
  if (_thread.joinable()) {
    _thread.join();
  }
  _started = NO;
}

- (void)addPeerWithUID:(NSString *)uid {
  if (_runtime == nullptr || uid.length == 0) {
    return;
  }
  _runtime->QueueAddPeer(std::string{uid.UTF8String});
}

- (void)submitText:(NSString *)text {
  if (_runtime == nullptr || text.length == 0) {
    return;
  }
  _runtime->QueueSend(std::string{text.UTF8String});
}

- (void)publishUID:(std::string const &)uid {
  NSString *const text = [NSString stringWithUTF8String:uid.c_str()] ?: @"";
  dispatch_async(dispatch_get_main_queue(), ^{
    self->_localUID = text;
    if (self->_onChange != nil) {
      self->_onChange();
    }
  });
}

- (void)publishTranscript:(std::string const &)transcript {
  NSString *const text = [NSString stringWithUTF8String:transcript.c_str()] ?: @"";
  NSArray<NSString *> *rows = @[];
  if (text.length > 0) {
    NSArray<NSString *> *const parts =
        [text componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet];
    NSMutableArray<NSString *> *filtered = [NSMutableArray array];
    for (NSString *part in parts) {
      if (part.length > 0) {
        [filtered addObject:part];
      }
    }
    rows = filtered;
  }
  dispatch_async(dispatch_get_main_queue(), ^{
    self->_timelineRows = rows;
    if (self->_onChange != nil) {
      self->_onChange();
    }
  });
}

@end
