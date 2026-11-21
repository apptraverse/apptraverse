#import "ChatBridge.h"
#include "../chat_ui_session.h"
#include "apptraverse/noninteractive_crt.h"
#include <cstdlib>
#include <filesystem>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
#include "apptraverse/platform/macos/macos_application_bridge.h"
#else
#include "apptraverse/platform/ios/ios_application_bridge.h"
#endif
using namespace apptraverse::chat::gui;
static NSString* Text(std::string const& value) {
  NSString* result=[[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding];
  if(!result){apptraverse::WriteFatalStderr("invalid chat UI text\n");std::abort();}return result;
}
static std::string Utf8(NSString* value) {
  NSData* bytes = [value dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
  if (!bytes) { apptraverse::WriteFatalStderr("cannot encode native chat text\n"); std::abort(); }
  if (bytes.length == 0) return {};
  return {static_cast<char const*>(bytes.bytes), bytes.length};
}
static NSDictionary* ControlSnapshot(ControlState const& state) {
  NSMutableArray* rows = [NSMutableArray arrayWithCapacity:state.rows.size()];
  for (auto const& row : state.rows) [rows addObject:Text(row)];
  return @{@"text":Text(state.text), @"rows":rows, @"enabled":@(state.enabled),
           @"visible":@(state.visible), @"readOnly":@(state.read_only)};
}

#if TARGET_OS_IOS
@interface ATChatBridge ()
- (void)publishMobileEnvironment;
@end
#endif

@implementation ATChatBridge {
  std::unique_ptr<Session> session;
  NSString* profile;
  void (^changed)(void);
  int lock;
  BOOL stopped,started;
#if TARGET_OS_OSX
  std::uint64_t placementRevision;
  bool ownInitialPlacement;
#else
  std::optional<apptraverse::ui::UiDeviceEnvironment> mobileEnvironment;
  std::optional<apptraverse::ui::UiDeviceEnvironment> submittedMobileEnvironment;
  std::optional<apptraverse::ui::UiWindowEnvironment> submittedMobileViewport;
  int mobileWidth, mobileHeight;
#endif
}
- (instancetype)initWithProfile:(NSString*)path name:(NSString*)name host:(BOOL)host initialHost:(NSString*)initialHost loadOnly:(BOOL)loadOnly changed:(void (^)(void))callback {
  if((self=[super init])){
    static bool const configured = [] {
      EnsureUiRegistration();
      auto factories = HeadlessUiFactories();
      NSString* language = [[NSLocale preferredLanguages].firstObject
          componentsSeparatedByString:@"-"].firstObject;
      factories.startup_language = [language isEqualToString:@"ru"]
          ? apptraverse::ui::UiLanguage::kRussian
          : apptraverse::ui::UiLanguage::kEnglish;
      ConfigureUiFactories(std::move(factories));
      return true;
    }();
    (void)configured;
    profile = [path copy];
    changed = [callback copy];
    lock = -1;
    __weak ATChatBridge* weakSelf = self;
    char const* nativePath = path.fileSystemRepresentation;
    if (!nativePath || nativePath[0] == '\0') {
      apptraverse::WriteFatalStderr("cannot resolve chat profile path\n"); std::abort();
    }
    session=std::make_unique<Session>(nativePath,Utf8(name),host,
      Utf8(initialHost),[weakSelf]{dispatch_async(dispatch_get_main_queue(),^{
        ATChatBridge* bridge=weakSelf;
        if(bridge&&!bridge->stopped&&bridge->session->ApplyPublication(static_cast<void*>(nullptr))) {
#if TARGET_OS_IOS
          [bridge publishMobileEnvironment];
#endif
          bridge->changed();
        }
      });},loadOnly);
  }return self;
}
- (void)registerApplePushToken:(NSString*)token {if(!stopped)session->RegisterPushToken(Utf8(token),true);}
- (BOOL)start {
  if(stopped)return NO;if(started)return YES;
  std::error_code error;std::filesystem::create_directories(profile.fileSystemRepresentation,error);
  if(error)return NO;
  std::filesystem::permissions(profile.fileSystemRepresentation,std::filesystem::perms::owner_all,error);
  if(error)return NO;
  lock=open([[profile stringByAppendingPathComponent:@".host.lock"] fileSystemRepresentation],O_CREAT|O_RDWR,0600);
  if(lock<0)return NO;
  if(flock(lock,LOCK_EX|LOCK_NB)!=0){close(lock);lock=-1;return NO;}
#if TARGET_OS_OSX
  session->PrepareDeviceEnvironment(apptraverse::ui::macos::ReadMacDeviceEnvironment());
#endif
  started=YES;session->Start();return YES;
}
- (void)stop {if(stopped)return;stopped=YES;session->Stop();if(lock>=0){close(lock);lock=-1;}}
- (void)dealloc {[self stop];}
- (void)checkpoint:(void (^)(BOOL))completion {
  if(stopped){completion(NO);return;}
  session->Checkpoint([completion](bool ok){dispatch_async(dispatch_get_main_queue(),^{completion(ok);});});
}
- (void)editDraft:(NSString*)text {if(!stopped)session->Proxy().Invoke(RootId(),&ChatUi::EditDraft,Utf8(text));}
- (void)editInvite:(NSString*)text {if(!stopped)session->Proxy().Invoke(RootId(),&ChatUi::EditInvite,Utf8(text));}
- (void)join {if(!stopped)session->Proxy().Invoke(RootId(),&ChatUi::Join);}
- (void)send {if(!stopped)session->Proxy().Invoke(RootId(),&ChatUi::Send);}
- (void)copyID {if(!stopped)session->Proxy().Invoke(RootId(),&ChatUi::Copy);}
- (void)clipboardResult:(BOOL)ok {if(!stopped)session->Proxy().Invoke(RootId(),&ChatUi::ClipboardResult,static_cast<bool>(ok));}
#if TARGET_OS_OSX
- (void)observeEnvironment {
  auto const* model = session->Mirror();
  if (stopped || !started || !model) return;
  session->Proxy().Invoke(model->device_state.id(),
      &apptraverse::ui::UiDeviceState::ObserveEnvironment,
      apptraverse::ui::macos::ReadMacDeviceEnvironment(),
      apptraverse::ui::UiEnvironmentReason::kSystemNotification);
}
- (void)observeWindow:(id)native commit:(BOOL)commit {
  NSWindow* window = native;
  auto const* model = session->Mirror();
  if (stopped || !model || !window.screen) return;
  bool const normal = !window.isMiniaturized && !window.isZoomed &&
      !(window.styleMask & NSWindowStyleMaskFullScreen);
  if (!model->device_state->PlacementFor(RootId())) {
    if (!normal) return;
    ownInitialPlacement = true;
  }
  // Non-normal rectangles must not replace the normal frame. A system
  // notification records DPI/monitor changes without changing user preference.
  session->Proxy().Invoke(model->device_state.id(),
      &apptraverse::ui::UiDeviceState::ObserveWindow,
      apptraverse::ui::macos::ReadMacWindowEnvironment(window, RootId()),
      commit && normal ? apptraverse::ui::UiEnvironmentReason::kUserWindowChange
                       : apptraverse::ui::UiEnvironmentReason::kSystemNotification);
}
- (BOOL)applyWindowPlacement:(id)native {
  NSWindow* window = native;
  auto const* model = session->Mirror();
  if (stopped || !model) return NO;
  auto const* placement = model->device_state->PlacementFor(RootId());
  if (!placement) return NO;
  if (placement->revision == placementRevision ||
      placement->resolution == apptraverse::ui::UiMonitorResolution::kUnavailable)
    return YES;
  if (ownInitialPlacement && placement->revision == 1) {
    placementRevision = placement->revision;
    return YES;
  }
  bool const preserveZoom = placementRevision != 0 && window.isZoomed;
  if (apptraverse::ui::macos::ApplyMacWindowPlacement(window, *placement, preserveZoom))
    placementRevision = placement->revision;
  return YES;
}
- (void)resetWindowPlacement { placementRevision = 0; ownInitialPlacement = false; }
#else
- (void)observeMobileSettings:(ATIosDeviceSettings)settings {
  if (stopped) return;
  mobileEnvironment = apptraverse::ui::ios::IosDeviceEnvironment(settings);
  if (!started) session->PrepareDeviceEnvironment(*mobileEnvironment);
  else [self publishMobileEnvironment];
}
- (void)reportMobileViewportWidth:(int)width height:(int)height {
  if (stopped || width < 1 || height < 1) return;
  mobileWidth = width; mobileHeight = height;
  [self publishMobileEnvironment];
}
- (void)publishMobileEnvironment {
  auto const* model = session->Mirror();
  if (stopped || !started || !model || !mobileEnvironment) return;
  auto const id = model->device_state.id();
  if (submittedMobileEnvironment != mobileEnvironment) {
    submittedMobileEnvironment = mobileEnvironment;
    session->Proxy().Invoke(id, &apptraverse::ui::UiDeviceState::ObserveEnvironment,
        *mobileEnvironment, apptraverse::ui::UiEnvironmentReason::kSystemNotification);
  }
  if (mobileWidth < 1 || mobileHeight < 1) return;
  auto const dpi = mobileEnvironment->system_dpi_x;
  apptraverse::ui::UiWindowEnvironment viewport{RootId(), {}, dpi, dpi,
      {0, 0, mobileWidth, mobileHeight}};
  if (submittedMobileViewport == viewport) return;
  submittedMobileViewport = viewport;
  session->Proxy().Invoke(id, &apptraverse::ui::UiDeviceState::ObserveWindow,
      viewport, apptraverse::ui::UiEnvironmentReason::kSystemNotification);
}
#endif
- (NSDictionary*)snapshot {
  auto const* model=session->Mirror();if(!model)return @{@"loaded":@NO};
  NSMutableArray* labels=[NSMutableArray array];
  for(unsigned i=0;i<static_cast<unsigned>(Label::Count);++i)[labels addObject:Text(model->Text(static_cast<Label>(i)))];
  auto const& data=model->connection;
  // These acceptance metrics come from the live transport, not the UI graph.
  auto const diagnostics=session->TransportView();
  NSMutableArray* controls = [NSMutableArray arrayWithCapacity:model->controls.size()];
  for (auto const& state : model->controls)
    [controls addObject:ControlSnapshot(state)];
  return @{@"loaded":@YES, @"controls":controls, @"labels":labels, @"title":Text(model->Title()),
    @"width":@(model->client_width()), @"height":@(model->client_height()), @"x":@(model->x()), @"y":@(model->y()),
    @"restoreWidth":@(model->restore_client_width()), @"restoreHeight":@(model->restore_client_height()),
    @"hasExplicitPlacement":@(model->HasExplicitPosition()),
    @"uid":Text(data.uid), @"invite":Text(model->editing.invite),
    @"editRevision":@(model->editing.revision), @"copyRequest":@(model->copy_request),
    @"status":Text(model->Get(Control::Status).text),
    @"ready":@(data.ready), @"in_room":@(data.in_room), @"online":@(data.online), @"host":@(model->editing.host),
    @"tcp":@(diagnostics.tcp_path_verified), @"pending":@(diagnostics.pending_packets),
    @"pending_progress":@(diagnostics.pending_progress_packets),
    @"participants":controls[static_cast<unsigned>(Control::People)][@"rows"],
    @"messages":controls[static_cast<unsigned>(Control::Messages)][@"rows"]};
}
@end
