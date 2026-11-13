#import "ChatBridge.h"
#include "apptraverse/chat_session.h"
#include <filesystem>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
static NSString* Text(std::string const& value) {
  return [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding];
}
@implementation ATChatBridge {
  std::unique_ptr<apptraverse::chat::ChatSession> session;
  NSString* profile;
  void (^changed)(void);
  int lock;
  BOOL stopped;
}
- (instancetype)initWithProfile:(NSString*)path name:(NSString*)name changed:(void (^)(void))callback {
  if ((self = [super init])) {
    profile = [path copy]; changed = [callback copy]; lock = -1;
    __weak ATChatBridge* weakSelf = self;
    session = std::make_unique<apptraverse::chat::ChatSession>(path.fileSystemRepresentation, name.UTF8String, [weakSelf] {
      dispatch_async(dispatch_get_main_queue(), ^{
        ATChatBridge* bridge = weakSelf;
        if (bridge && !bridge->stopped) bridge->changed();
      });
    });
  }
  return self;
}
- (BOOL)start {
  std::error_code error;
  std::filesystem::create_directories(profile.fileSystemRepresentation, error);
  if (error) return NO;
  std::filesystem::permissions(profile.fileSystemRepresentation, std::filesystem::perms::owner_all, error);
  if (error) return NO;
  lock = open([[profile stringByAppendingPathComponent:@".host.lock"] fileSystemRepresentation], O_CREAT | O_RDWR, 0600);
  if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) != 0) return NO;
  session->Start(); return YES;
}
- (void)stop {
  if (stopped) return;
  stopped = YES;
  session->Stop();
  if (lock >= 0) { close(lock); lock = -1; }
}
- (void)checkpoint:(void (^)(BOOL))completion {
  if (stopped) { completion(NO); return; }
  session->Checkpoint([completion](bool ok) {
    dispatch_async(dispatch_get_main_queue(), ^{ completion(ok); });
  });
}
- (void)dealloc { [self stop]; }
- (void)createRoom { if (!stopped) session->CreateRoom(); }
- (void)joinRoom:(NSString*)invite { if (!stopped) session->JoinRoom(invite.UTF8String); }
- (void)send:(NSString*)text { if (!stopped) session->Send(text.UTF8String); }
- (NSDictionary*)snapshot {
  auto view = session->View();
  NSMutableArray* participants = [NSMutableArray array];
  for (auto const& p : view.participants) {
    using apptraverse::chat::Presence;
    auto presence = p.presence == Presence::Online ? "online" : p.presence == Presence::Offline ? "offline" : "unknown";
    [participants addObject:Text(p.name + " [" + presence + "]")];
  }
  NSMutableArray* history = [NSMutableArray array];
  for (auto const& message : view.messages) [history addObject:Text(message)];
  return @{ @"uid": Text(view.local_uid), @"invite": Text(view.invite),
    @"status": Text(view.status), @"ready": @(view.ready), @"in_room": @(view.in_room),
    @"online": @(view.locally_online), @"tcp": @(view.tcp_path_verified),
    @"pending": @(view.pending_packets), @"participants": participants, @"messages": history };
}
@end
