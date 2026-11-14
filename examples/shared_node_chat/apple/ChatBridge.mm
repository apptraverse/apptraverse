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
struct ApplePresentation:Presentation {
  std::function<void(Control,ControlState const&)> update;
  void Present(Control c,ControlState const& state,ChatUi const&)override{update(c,state);}
  void Remove(Control)override{}
};
@interface ATChatBridge ()
- (void)render:(unsigned)index state:(ControlState const&)state;
@end
@implementation ATChatBridge {
  std::unique_ptr<Session> session;
  std::unique_ptr<ApplePresentation> presentation;
  NSMutableArray<NSDictionary*>* controls;
  NSString* profile;
  void (^changed)(void);
  int lock;
  BOOL stopped,started;
}
- (instancetype)initWithProfile:(NSString*)path name:(NSString*)name host:(BOOL)host initialHost:(NSString*)initialHost loadOnly:(BOOL)loadOnly changed:(void (^)(void))callback {
  if((self=[super init])){
    profile=[path copy];changed=[callback copy];lock=-1;
    controls=[NSMutableArray array];
    for(unsigned i=0;i<static_cast<unsigned>(Control::Count);++i)[controls addObject:@{}];
    __weak ATChatBridge* weakSelf=self;
    presentation=std::make_unique<ApplePresentation>();
    presentation->update=[weakSelf](Control c,ControlState const& state){
      ATChatBridge* bridge=weakSelf;if(bridge)[bridge render:static_cast<unsigned>(c) state:state];
    };
    session=std::make_unique<Session>(path.fileSystemRepresentation,name.UTF8String,host,
      initialHost.UTF8String,[weakSelf]{dispatch_async(dispatch_get_main_queue(),^{
        ATChatBridge* bridge=weakSelf;
        if(bridge&&!bridge->stopped&&bridge->session->ApplyPublication(*bridge->presentation))bridge->changed();
      });},loadOnly);
  }return self;
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
- (void)editDraft:(NSString*)text {if(!stopped)session->Proxy().Invoke(RootId(),&ChatUi::EditDraft,std::string{text.UTF8String});}
- (void)editInvite:(NSString*)text {if(!stopped)session->Proxy().Invoke(RootId(),&ChatUi::EditInvite,std::string{text.UTF8String});}
- (void)join {if(!stopped)session->Proxy().Invoke(RootId(),&ChatUi::Join);}
- (void)send {if(!stopped)session->Proxy().Invoke(RootId(),&ChatUi::Send);}
- (void)clipboardResult:(BOOL)ok {if(!stopped)session->Proxy().Invoke(RootId(),&ChatUi::ClipboardResult,static_cast<bool>(ok));}
- (void)resizeWidth:(int)width height:(int)height {if(!stopped)session->Proxy().Invoke(RootId(),&ChatUi::Resize,width,height);}
- (NSDictionary*)snapshot {
  auto const* model=session->Mirror();if(!model)return @{@"loaded":@NO};
  NSMutableArray* labels=[NSMutableArray array];
  for(unsigned i=0;i<static_cast<unsigned>(Label::Count);++i)[labels addObject:Text(model->Text(static_cast<Label>(i)))];
  auto const& data=model->connection;
  return @{@"loaded":@YES,@"controls":[controls copy],@"labels":labels,@"title":Text(model->Title()),
    @"uid":Text(data.uid),@"invite":Text(model->editing.invite),@"status":Text(model->Get(Control::Status).text),
    @"ready":@(data.ready),@"in_room":@(data.in_room),@"online":@(data.online),
    @"participants":controls[static_cast<unsigned>(Control::People)][@"rows"],
    @"messages":controls[static_cast<unsigned>(Control::Messages)][@"rows"],
    @"width":@(model->layout.width),@"height":@(model->layout.height),
    @"minimumWidth":@(model->layout.minimum_width),@"minimumHeight":@(model->layout.minimum_height),
    @"gap":@(model->layout.gap)};
}
@end
