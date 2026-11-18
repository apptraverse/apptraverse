#import "ChatBridge.h"
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
#include "AppleComposition.h"
#include "../chat_controls.h"
#include "apptraverse/ui/ui_constraint_layout.h"
#endif
#include "../chat_ui_session.h"
#include "apptraverse/noninteractive_crt.h"
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
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
#if TARGET_OS_OSX
static apptraverse::ui::UiDeviceEnvironment AppleEnvironment() {
  using namespace apptraverse::ui;
  UiDeviceEnvironment result;
  result.supported_settings=kSystemDarkMode|kSystemHighContrast|kSystemReducedMotion;
  result.dark=[[NSApp.effectiveAppearance bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua,NSAppearanceNameDarkAqua]] isEqualToString:NSAppearanceNameDarkAqua];
  result.high_contrast=NSWorkspace.sharedWorkspace.accessibilityDisplayShouldIncreaseContrast;
  result.reduced_motion=NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;
  NSFont* font=[NSFont systemFontOfSize:NSFont.systemFontSize];
  result.message_font.family=Utf8(font.familyName);
  double scale=NSScreen.mainScreen.backingScaleFactor;
  result.system_dpi_x=result.system_dpi_y=static_cast<unsigned>(std::lround(96*scale));
  result.message_font.height=static_cast<int>(std::lround(font.pointSize*scale));
  result.message_font.weight=400;
  auto* colors=&result.colors;
  [NSApp.effectiveAppearance performAsCurrentDrawingAppearance:^{
    for(NSColor* color in @[NSColor.windowBackgroundColor,NSColor.labelColor,NSColor.textBackgroundColor,NSColor.disabledControlTextColor,NSColor.separatorColor]){
      NSColor* rgb=[color colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
      if(!rgb){apptraverse::WriteFatalStderr("cannot read native Apple color\n");std::abort();}
      CGFloat r,g,b,a;[rgb getRed:&r green:&g blue:&b alpha:&a];
      colors->push_back(static_cast<unsigned>(std::lround(r*255))|(static_cast<unsigned>(std::lround(g*255))<<8)|(static_cast<unsigned>(std::lround(b*255))<<16));
    }
  }];
  double top=NSMaxY(NSScreen.screens.firstObject.frame);
  for(NSScreen* screen in NSScreen.screens){
    UiMonitorEnvironment monitor;
    monitor.key=Utf8([screen.deviceDescription[@"NSScreenNumber"] stringValue]);
    NSRect f=screen.frame,w=screen.visibleFrame;
    monitor.bounds={static_cast<int>(f.origin.x),static_cast<int>(top-NSMaxY(f)),static_cast<int>(f.size.width),static_cast<int>(f.size.height)};
    monitor.work_area={static_cast<int>(w.origin.x),static_cast<int>(top-NSMaxY(w)),static_cast<int>(w.size.width),static_cast<int>(w.size.height)};
    monitor.primary=screen==NSScreen.screens.firstObject;
    result.monitors.push_back(std::move(monitor));
  }
  return result;
}
static constexpr unsigned AppleSlots[]={110,101,102,103,104,111,105,112,106,107,108,109};
static apptraverse::ui::UiConstraintLayoutResult AppleLayout(ChatUi const& model,double width,double height) {
  using namespace apptraverse::ui;
  auto root=*model.presenter->LayoutRoot();
  auto shown=[&](unsigned slot){for(unsigned i=0;i<static_cast<unsigned>(Control::Count);++i)if(AppleSlots[i]==slot)return model.Get(static_cast<Control>(i)).visible;std::abort();};
  auto filter=[&](auto&& self,UiLayoutNode& node)->void{
    std::erase_if(node.children,[&](auto const& child){return child.flow==UiLayoutFlow::kLeaf&&!shown(child.control_id);});
    for(auto& child:node.children)self(self,child);
  };
  filter(filter,root);
  NSFont* font=[NSFont systemFontOfSize:NSFont.systemFontSize];
  std::vector<UiLayoutMeasurement> measures;
  for(unsigned i=0;i<static_cast<unsigned>(Control::Count);++i){
    auto role=static_cast<Control>(i);auto const& state=model.Get(role);
    if(!state.visible)continue;
    NSFont* measured=role==Control::Status?[NSFont systemFontOfSize:NSFont.smallSystemFontSize]:font;
    double text_width=[Text(state.text) sizeWithAttributes:@{NSFontAttributeName:measured}].width;
    double line=std::ceil(measured.ascender-measured.descender+measured.leading);
    bool edit=role==Control::Input||role==Control::Invite;
    bool button=role==Control::Send||role==Control::Join||role==Control::Copy;
    bool rows=role==Control::Messages||role==Control::People;
    double minw=text_width,minh=line,preferred=text_width;
    if(rows){minw=0;minh=0;preferred=0;} // Scrollable contents; model owns viewport minima.
    if(button){
      NSButtonCell* cell=[[NSButtonCell alloc] initTextCell:Text(state.text)];
      cell.bezelStyle=NSBezelStyleRounded;[cell setButtonType:NSButtonTypeMomentaryPushIn];cell.font=measured;
      NSSize size=cell.cellSize;minw=preferred=size.width;minh=size.height;
    }
    if(edit){
      NSTextFieldCell* cell=[[NSTextFieldCell alloc] initTextCell:Text(state.text)];
      cell.bezeled=YES;cell.editable=YES;cell.font=measured;
      NSSize size=cell.cellSize;preferred=size.width;minh=size.height;
      cell.stringValue=@"";minw=cell.cellSize.width;
    }
    measures.push_back({AppleSlots[i],minw,minh,preferred,minh});
  }
  auto result=ComputeUiConstraintLayout(root,width,height,measures);
  if(!result.valid){apptraverse::WriteFatalStderr(result.error.c_str());std::abort();}
  return result;
}
#endif
struct ApplePresentation:Presentation {
  std::function<void(Control,ControlState const&)> update;
  std::function<void(ChatUi const&)> updateWindow;
  std::function<void()> removeWindow;
  void Present(Control c,ControlState const& state,ChatUi const&)override{update(c,state);}
  void Remove(Control)override{}
  void PresentWindow(ChatUi const& model)override{updateWindow(model);}
  void RemoveWindow()override{removeWindow();}
};
@interface ATChatBridge ()
- (void)render:(unsigned)index state:(ControlState const&)state;
- (void)renderWindow:(ChatUi const&)model;
@end
@implementation ATChatBridge {
  std::unique_ptr<Session> session;
  std::unique_ptr<ApplePresentation> presentation;
  NSMutableArray<NSDictionary*>* controls;
  NSDictionary* windowState;
  NSString* profile;
  void (^changed)(void);
  int lock;
#if TARGET_OS_OSX
  CGSize viewport;
#endif
  BOOL stopped,started;
}
- (instancetype)initWithProfile:(NSString*)path name:(NSString*)name host:(BOOL)host initialHost:(NSString*)initialHost loadOnly:(BOOL)loadOnly changed:(void (^)(void))callback {
  if((self=[super init])){
    EnsureUiRegistration();
    profile=[path copy];changed=[callback copy];lock=-1;
    controls=[NSMutableArray array];
    for(unsigned i=0;i<static_cast<unsigned>(Control::Count);++i)[controls addObject:@{}];
    __weak ATChatBridge* weakSelf=self;
    presentation=std::make_unique<ApplePresentation>();
    presentation->update=[weakSelf](Control c,ControlState const& state){
      ATChatBridge* bridge=weakSelf;if(bridge)[bridge render:static_cast<unsigned>(c) state:state];
    };
    presentation->updateWindow=[weakSelf](ChatUi const& model){
      ATChatBridge* bridge=weakSelf;if(bridge)[bridge renderWindow:model];
    };
    presentation->removeWindow=[weakSelf]{
      ATChatBridge* bridge=weakSelf;if(bridge)bridge->windowState=nil;
    };
    char const* nativePath = path.fileSystemRepresentation;
    if (!nativePath || nativePath[0] == '\0') {
      apptraverse::WriteFatalStderr("cannot resolve chat profile path\n"); std::abort();
    }
    session=std::make_unique<Session>(nativePath,Utf8(name),host,
      Utf8(initialHost),[weakSelf]{dispatch_async(dispatch_get_main_queue(),^{
        ATChatBridge* bridge=weakSelf;
        if(bridge&&!bridge->stopped&&bridge->session->ApplyPublication(*bridge->presentation))bridge->changed();
      });},loadOnly);
  }return self;
}
- (void)renderWindow:(ChatUi const&)model {
  windowState=@{@"width":@(model.client_width()),@"height":@(model.client_height()),
    @"x":@(model.x()),@"y":@(model.y()),
    @"restoreWidth":@(model.restore_client_width()),@"restoreHeight":@(model.restore_client_height()),
    @"hasExplicitPlacement":@(model.HasExplicitPosition()),
#if TARGET_OS_OSX
    @"minimumWidth":@0,@"minimumHeight":@0
#else
    @"minimumWidth":@(model.layout.minimum_width),@"minimumHeight":@(model.layout.minimum_height)
#endif
  };
}
- (void)render:(unsigned)index state:(ControlState const&)state {
  NSMutableArray* rows=[NSMutableArray array];for(auto const& row:state.rows)[rows addObject:Text(row)];
  controls[index]=@{@"text":Text(state.text),@"rows":rows,@"enabled":@(state.enabled),
    @"visible":@(state.visible),@"readOnly":@(state.read_only),@"x":@(state.frame.x),
    @"y":@(state.frame.y),@"width":@(state.frame.width),@"height":@(state.frame.height)};
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
  ConfigureAppleComposition(AppleEnvironment());
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
- (void)clipboardResult:(BOOL)ok {if(!stopped)session->Proxy().Invoke(RootId(),&ChatUi::ClipboardResult,static_cast<bool>(ok));}
- (void)resizeWidth:(int)width height:(int)height {
#if TARGET_OS_OSX
  if(stopped||width<=0||height<=0)return;
  viewport=CGSizeMake(width,height);changed();
#else
  if(!stopped)session->Proxy().Invoke(RootId(),&ChatUi::Resize,width,height);
#endif
}
- (void)reportFrameX:(int)x y:(int)y width:(int)width height:(int)height
     updatesRestore:(BOOL)updatesRestore {
#if TARGET_OS_OSX
  if(stopped)return;
  viewport=CGSizeMake(width,height);
  changed();
#else
  if(!stopped) session->Proxy().Invoke<apptraverse::ui::UiDesktopWindow>(RootId(),
      &apptraverse::ui::UiDesktopWindow::SubmitFrameFromNative,
      x,y,width,height,static_cast<bool>(updatesRestore));
#endif
}
- (void)observeEnvironment {
#if TARGET_OS_OSX
  if(stopped)return;
  auto baseline=AppleEnvironment();
  session->Proxy().EnqueueModelWork([baseline=std::move(baseline)](ae::Domain& domain){
    auto* root=domain.Find(RootId()).as<ChatUi>();
    root->device_state->ObserveEnvironment(baseline,apptraverse::ui::UiEnvironmentReason::kSystemNotification);
  });
#endif
}
- (void)reportLocalWindow:(NSString*)monitor x:(int)x y:(int)y width:(int)width height:(int)height dpi:(int)dpi userChange:(BOOL)userChange {
#if TARGET_OS_OSX
  if(stopped)return;
  auto key=Utf8(monitor);
  session->Proxy().EnqueueModelWork([key=std::move(key),x,y,width,height,dpi,userChange](ae::Domain& domain){
    auto* root=domain.Find(RootId()).as<ChatUi>();
    apptraverse::ui::UiWindowEnvironment frame;
    frame.window_id=RootId();frame.monitor_key=key;frame.frame_dip={x,y,width,height};frame.dpi_x=frame.dpi_y=dpi;
    root->device_state->ObserveWindow(frame,userChange?apptraverse::ui::UiEnvironmentReason::kUserWindowChange:apptraverse::ui::UiEnvironmentReason::kSystemNotification);
  });
#endif
}
- (NSDictionary*)snapshot {
  auto const* model=session->Mirror();if(!model)return @{@"loaded":@NO};
  NSMutableArray* labels=[NSMutableArray array];
  for(unsigned i=0;i<static_cast<unsigned>(Label::Count);++i)[labels addObject:Text(model->Text(static_cast<Label>(i)))];
#if TARGET_OS_OSX
  auto laid=AppleLayout(*model,viewport.width>0?viewport.width:model->client_width(),viewport.height>0?viewport.height:model->client_height());
  auto minimum=AppleLayout(*model,0,0);
  for(auto const& rect:laid.controls){
    for(unsigned i=0;i<static_cast<unsigned>(Control::Count);++i)if(AppleSlots[i]==rect.control_id){
      auto value=[controls[i] mutableCopy];value[@"x"]=@(rect.x);value[@"y"]=@(rect.y);
      value[@"width"]=@(rect.width);value[@"height"]=@(rect.height);controls[i]=value;
    }
  }
#endif
  auto const& data=model->connection;
  // These acceptance metrics come from the live transport, not the UI graph.
  auto const diagnostics=session->TransportView();
  NSMutableDictionary* result=[windowState mutableCopy];
  if(!result){apptraverse::WriteFatalStderr("chat window presenter missing\n");std::abort();}
#if TARGET_OS_OSX
  result[@"minimumWidth"]=@(minimum.content_width);result[@"minimumHeight"]=@(minimum.content_height);
  result[@"restoreOuter"]=@NO;
  if(auto const* placement=model->device_state->PlacementFor(RootId())){
    auto const& monitors=model->device_state->environment.monitors;
    auto monitor=std::find_if(monitors.begin(),monitors.end(),[&](auto const& item){return item.key==placement->resolved_monitor;});
    if(monitor!=monitors.end()){
      auto const& f=placement->frame_dip;auto const& w=monitor->work_area;
      result[@"hasExplicitPlacement"]=@YES;result[@"restoreOuter"]=@YES;
      result[@"x"]=@(w.x+f.x);result[@"y"]=@(w.y+f.y);
      result[@"restoreWidth"]=@(f.width);result[@"restoreHeight"]=@(f.height);
    }
  }
#endif
  [result addEntriesFromDictionary:@{@"loaded":@YES,@"controls":[controls copy],@"labels":labels,@"title":Text(model->Title()),
    @"uid":Text(data.uid),@"invite":Text(model->editing.invite),
    @"editRevision":@(model->editing.revision),@"status":Text(model->Get(Control::Status).text),
    @"ready":@(data.ready),@"in_room":@(data.in_room),@"online":@(data.online),@"host":@(model->editing.host),
    @"tcp":@(diagnostics.tcp_path_verified),@"pending":@(diagnostics.pending_packets),
    @"pending_progress":@(diagnostics.pending_progress_packets),
    @"participants":controls[static_cast<unsigned>(Control::People)][@"rows"],
    @"messages":controls[static_cast<unsigned>(Control::Messages)][@"rows"],
#if TARGET_OS_OSX
    @"gap":@(model->layout_state->EffectiveRoot().gap)
#else
    @"gap":@(model->layout.gap)
#endif
  }];
  return result;
}
@end
