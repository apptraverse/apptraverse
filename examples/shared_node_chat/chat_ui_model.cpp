#include "chat_ui_model.h"
#include <algorithm>
#include <cassert>
#include <mutex>
#include "apptraverse/ui/ui_presented_window_composition.h"

namespace apptraverse::chat::gui {
APPTRAVERSE_REGISTER(ChatUi);
APPTRAVERSE_REGISTER(ChatUiEvent);
APPTRAVERSE_REGISTER(ChatControlPresenter);
APPTRAVERSE_REGISTER(ChatWindowPresenter);
void EnsureUiRegistration(){
  static std::once_flag once;
  std::call_once(once,[]{ EnsureObjectRegistration();
    (void)&g_apptraverse_registrar_ChatUi;
    (void)&g_apptraverse_registrar_ChatUiEvent;
    (void)&g_apptraverse_registrar_ChatControlPresenter;
    (void)&g_apptraverse_registrar_ChatWindowPresenter;
  });
}
void ChatWindowPresenter::OnLoad(){ OnModelChanged(); }
void ChatWindowPresenter::OnModelChanged(){
  assert(presentation_host && model_proxy);
  static_cast<Presentation*>(presentation_host)->PresentWindow(static_cast<ChatUi const&>(*window));
}
void ChatWindowPresenter::OnUnload(){
  static_cast<Presentation*>(presentation_host)->RemoveWindow();
}
bool ChatControlPresenter::ReadyForPresentation()const{
  return model->presenter->presentation_loaded;
}
void ChatControlPresenter::OnLoad(){ OnModelChanged(); }
void ChatControlPresenter::OnModelChanged(){
  assert(presentation_host && model_proxy);
  static_cast<Presentation*>(presentation_host)->Present(
      static_cast<Control>(control),model->controls.at(control),*model);
}
void ChatControlPresenter::OnUnload(){
  static_cast<Presentation*>(presentation_host)->Remove(static_cast<Control>(control));
}
std::string ChatUi::Text(Label label)const { return labels.at(static_cast<unsigned>(label))->Resolve(presenter->locale->language_index()); }
std::string ChatUi::Title()const { return Text(Label::Title)+" - "+editing.name; }
ControlState const& ChatUi::Get(Control c)const { return controls.at(static_cast<unsigned>(c)); }
namespace {
ChatUiEvent::ptr Make(ChatUi& model,Action action){
  auto event=ChatUiEvent::ptr::Create(ae::CreateWith{*model.domain});
  event->action=action; event->text.clear(); event->second.clear();
  event->flag=false; event->revision=0; event->first_value=0;
  event->connection={}; return event;
}
}
void ChatUi::EditDraft(std::string text){ auto e=Make(*this,Action::Draft); e->text=std::move(text); Commit(e); }
void ChatUi::EditInvite(std::string text){ auto e=Make(*this,Action::Invite); e->text=std::move(text); Commit(e); }
void ChatUi::Resize(int width,int height){
  if(width<=0||height<=0)return; // Native surface temporarily unavailable.
  SubmitFrameFromNative(0,0,width,height,false);
}
void ChatUi::OnClientSizeChanged(){ RefreshControls(); }
void ChatUi::ClipboardResult(bool ok){ auto e=Make(*this,Action::Feedback);e->first_value=static_cast<int>(ok?Label::Copied:Label::CopyFailed);Commit(e); }
void ChatUi::Configure(bool host,std::string name,std::string initial_host){
  if(editing.configured)return;
  auto e=Make(*this,Action::Configure);e->flag=host;e->text=std::move(name);e->second=std::move(initial_host);Commit(e);
}
void ChatUi::Join(){ if(connection.ready&&!connection.in_room&&!editing.host) join(editing.invite); }
void ChatUi::Send(){
  if(!connection.ready||!connection.in_room||editing.sending||
     editing.draft.find_first_not_of(" \t\r\n")==std::string::npos) return;
  if(editing.draft.size()>8192){auto e=Make(*this,Action::Feedback);e->first_value=static_cast<int>(Label::TooLong);Commit(e);return;}
  auto text=editing.draft;auto revision=editing.revision;
  Commit(Make(*this,Action::SendStarted)); send(std::move(text),revision);
}
void ChatUi::CompleteSend(std::uint64_t revision,bool committed){
  auto e=Make(*this,Action::SendCompleted);e->revision=revision;e->flag=committed;Commit(e);
}
void ChatUi::AcceptTransport(ChatView view){
  auto e=Make(*this,Action::Transport);
  e->connection.uid=std::move(view.local_uid);e->connection.invite=std::move(view.invite);
  e->connection.status=std::move(view.status);e->connection.messages=std::move(view.messages);
  e->connection.ready=view.ready;e->connection.in_room=view.in_room;e->connection.online=view.locally_online;
  for(auto const& p:view.participants){
    auto const state=p.presence==Presence::Online?Label::Online:p.presence==Presence::Offline?Label::Offline:Label::Unknown;
    e->connection.people.push_back(p.name+" ["+Text(state)+"]");
  }
  Commit(e);
  if(connection.ready&&!initial_action_submitted_){
    initial_action_submitted_=true;
    if(!connection.in_room){if(editing.host)create();else if(!editing.invite.empty())join(editing.invite);}
  }
}
void ChatUi::Apply(ChatUiEvent const& e){
  switch(e.action){
    case Action::Draft: editing.draft=e.text;++editing.revision;editing.error_label=-1;break;
    case Action::Invite: editing.invite=e.text;editing.error_label=-1;break;
    case Action::SendStarted: editing.sending=true;editing.error_label=-1;break;
    case Action::SendCompleted:
      editing.sending=false;
      if(e.flag){if(editing.revision==e.revision)editing.draft.clear();}
      else editing.error_label=static_cast<int>(Label::Rejected);
      break;
    case Action::Transport: connection=e.connection;
      if(!connection.invite.empty())editing.invite=connection.invite;break;
    case Action::Configure: editing.host=e.flag;editing.name=e.text;
      if(!e.second.empty())editing.invite=e.second;
      editing.configured=true;break;
    case Action::Feedback: editing.error_label=e.first_value;break;
  }
  RefreshControls(); NoteMaterializedChange();
}
void ChatUi::RefreshControls(){
  auto& values=controls;
  auto at=[&](Control c)->ControlState&{return values.at(static_cast<unsigned>(c));};
  for(auto& c:values){c.enabled=true;c.visible=true;c.read_only=false;}
  at(Control::UidLabel).text=Text(editing.host?Label::MyId:Label::HostId);
  at(Control::Uid).text=connection.uid.empty()?Text(Label::Connecting):connection.uid;
  at(Control::Uid).read_only=true;at(Control::Uid).visible=editing.host;
  at(Control::Copy).text=Text(Label::Copy);at(Control::Copy).visible=editing.host;
  at(Control::Copy).enabled=connection.ready&&!connection.uid.empty();
  at(Control::Invite).text=editing.invite;at(Control::Invite).visible=!editing.host;
  at(Control::Invite).read_only=connection.in_room;
  at(Control::Join).text=Text(Label::Join);at(Control::Join).visible=!editing.host;
  at(Control::Join).enabled=connection.ready&&!connection.in_room;
  at(Control::PeopleLabel).text=Text(Label::People);at(Control::People).rows=connection.people;
  at(Control::MessagesLabel).text=Text(Label::Messages);
  at(Control::Messages).rows=connection.messages;at(Control::Messages).read_only=true;
  at(Control::Input).text=editing.draft;at(Control::Input).enabled=connection.ready&&connection.in_room;
  at(Control::Send).text=Text(Label::Send);at(Control::Send).enabled=connection.ready&&connection.in_room&&!editing.sending;
  at(Control::Status).text=editing.error_label>=0?Text(static_cast<Label>(editing.error_label)):
    Text(connection.online?Label::Online:Label::Offline)+" | "+connection.status;
  int const m=layout.margin,g=layout.gap,r=client_width()-m,x=m+layout.label_width;
  int const people=std::clamp(client_width()/4,130,210),mx=m+people+g,mw=r-mx;
  int const iy=client_height()-70,bh=std::max(70,iy-g-layout.body_top);
  auto rect=[&](Control c,int xx,int yy,int w,int h){at(c).frame={xx,yy,w,h};};
  rect(Control::UidLabel,m,17,layout.label_width-g,20);
  rect(Control::Uid,x,m,r-x-g-layout.button_width,26);at(Control::Invite).frame=at(Control::Uid).frame;
  rect(Control::Copy,r-layout.button_width,m,layout.button_width,26);at(Control::Join).frame=at(Control::Copy).frame;
  rect(Control::PeopleLabel,m,50,people,20);rect(Control::MessagesLabel,mx,50,mw,20);
  rect(Control::People,m,layout.body_top,people,bh+34);rect(Control::Messages,mx,layout.body_top,mw,bh);
  rect(Control::Input,mx,iy,std::max(60,mw-g-76),layout.input_height);
  rect(Control::Send,r-76,iy,76,layout.input_height);rect(Control::Status,m,client_height()-30,r-m,22);
}
#ifdef APPTRAVERSE_CHAT_UI_PRODUCER
ChatUi::ptr BuildUi(ae::Domain& domain){
  ae::Domain::ObjectIdSequence ids{domain,ae::ObjId{9501},ae::ObjId{9999}};
  auto app=ChatUi::ptr::Create(ae::CreateWith{domain}.with_id(RootId()));
  app->connection={}; app->editing={}; app->editing.error_label=-1;
  char const* texts[]={"SharedNode Chat","My Aether ID","Host Aether ID","Copy","Join",
    "Participants","Messages","Send","Message","Done","Connecting...","Online","Offline",
    "Unknown","Message is too long (maximum 8192 UTF-8 bytes).","Could not save the message.",
    "Aether ID copied","Could not copy Aether ID","Quit","Edit","Cut","Paste","Select All"};
  for(auto text:texts){
    auto label=ui::UiLocalizationString::ptr::Create(ae::CreateWith{domain});
    auto str=ui::UiString::ptr::Create(ae::CreateWith{domain});str->utf8=text;label->strings={str};
    label->SetObjectScope({{}, {}, true});str->SetObjectScope({{}, {}, true});app->labels.push_back(label);
  }
  auto locale=ui::UiLocaleSettings::ptr::Create(ae::CreateWith{domain});
  locale->BootstrapLanguageIndex(0);
  auto window=ChatWindowPresenter::ptr::Create(ae::CreateWith{domain});
  ui::ComposePresentedWindow(app,window,app->labels.at(static_cast<unsigned>(Label::Title)),locale);
  app->BootstrapFrame(0,0,840,560);
  app->layout={400,300,12,8,100,100,70,28};
  app->controls.resize(static_cast<unsigned>(Control::Count));
  for(auto& control:app->controls) control={};
  app->RefreshControls();
  for(unsigned i=0;i<app->controls.size();++i){
    auto presenter=ChatControlPresenter::ptr::Create(ae::CreateWith{domain});
    presenter->model=app;presenter->control=i;app->presenters.push_back(presenter);
  }
  return app;
}
#endif
} // namespace apptraverse::chat::gui
