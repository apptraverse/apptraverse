#import "ChatBridge.h"
#include "../chat_ui_session.h"
#include "apptraverse/noninteractive_crt.h"
#include <cstdlib>
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
    @"minimumWidth":@(model.layout.minimum_width),@"minimumHeight":@(model.layout.minimum_height)};
}
- (void)render:(unsigned)index state:(ControlState const&)state {
  NSMutableArray* rows=[NSMutableArray array];for(auto const& row:state.rows)[rows addObject:Text(row)];
  controls[index]=@{@"text":Text(state.text),@"rows":rows,@"enabled":@(state.enabled),
    @"visible":@(state.visible),@"readOnly":@(state.read_only),@"x":@(state.frame.x),
    @"y":@(state.frame.y),@"width":@(state.frame.width),@"height":@(state.frame.height)};
}
- (BOOL)start {
  if(stopped)return NO;if(started)return YES;
  std::error_code error;std::filesystem::create_directories(profile.fileSystemRepresentation,error);
  if(error)return NO;
  std::filesystem::permissions(profile.fileSystemRepresentation,std::filesystem::perms::owner_all,error);
  if(error)return NO;
  lock=open([[profile stringByAppendingPathComponent:@".host.lock"] fileSystemRepresentation],O_CREAT|O_RDWR,0600);
  if(lock<0)return NO;
  if(flock(lock,LOCK_EX|LOCK_NB)!=0){close(lock);lock=-1;return NO;}
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
- (void)resizeWidth:(int)width height:(int)height {if(!stopped)session->Proxy().Invoke(RootId(),&ChatUi::Resize,width,height);}
- (void)reportFrameX:(int)x y:(int)y width:(int)width height:(int)height
     updatesRestore:(BOOL)updatesRestore {
  if(!stopped) session->Proxy().Invoke<apptraverse::ui::UiDesktopWindow>(RootId(),
      &apptraverse::ui::UiDesktopWindow::SubmitFrameFromNative,
      x,y,width,height,static_cast<bool>(updatesRestore));
}
- (NSDictionary*)snapshot {
  auto const* model=session->Mirror();if(!model)return @{@"loaded":@NO};
  NSMutableArray* labels=[NSMutableArray array];
  for(unsigned i=0;i<static_cast<unsigned>(Label::Count);++i)[labels addObject:Text(model->Text(static_cast<Label>(i)))];
  auto const& data=model->connection;
  // These acceptance metrics come from the live transport, not the UI graph.
  auto const diagnostics=session->TransportView();
  NSMutableDictionary* result=[windowState mutableCopy];
  if(!result){apptraverse::WriteFatalStderr("chat window presenter missing\n");std::abort();}
  [result addEntriesFromDictionary:@{@"loaded":@YES,@"controls":[controls copy],@"labels":labels,@"title":Text(model->Title()),
    @"uid":Text(data.uid),@"invite":Text(model->editing.invite),@"status":Text(model->Get(Control::Status).text),
    @"ready":@(data.ready),@"in_room":@(data.in_room),@"online":@(data.online),@"host":@(model->editing.host),
    @"tcp":@(diagnostics.tcp_path_verified),@"pending":@(diagnostics.pending_packets),
    @"pending_progress":@(diagnostics.pending_progress_packets),
    @"participants":controls[static_cast<unsigned>(Control::People)][@"rows"],
    @"messages":controls[static_cast<unsigned>(Control::Messages)][@"rows"],
    @"gap":@(model->layout.gap)}];
  return result;
}
@end
