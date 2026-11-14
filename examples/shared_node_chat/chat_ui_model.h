#pragma once
#include <functional>
#include <string>
#include <vector>
#include "apptraverse/chat_session.h"
#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/presenter.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::chat::gui {
enum class Control : unsigned { UidLabel, Uid, Copy, Invite, Join, PeopleLabel,
  People, MessagesLabel, Messages, Input, Send, Status, Count };
enum class Label : unsigned { Title, MyId, HostId, Copy, Join, People, Messages,
  Send, Message, Done, Connecting, Online, Offline, Unknown, TooLong, Rejected,
  Copied, CopyFailed, Count };
struct Rect { int x, y, width, height; AE_REFLECT_MEMBERS(x,y,width,height) };
struct ControlState {
  std::string text;
  std::vector<std::string> rows;
  Rect frame;
  bool enabled, visible, read_only;
  AE_REFLECT_MEMBERS(text,rows,frame,enabled,visible,read_only)
};
struct ConnectionState {
  std::string uid, invite, status;
  std::vector<std::string> people, messages;
  bool ready, in_room, online;
  AE_REFLECT_MEMBERS(uid,invite,status,people,messages,ready,in_room,online)
};
struct EditingState {
  std::string name, draft, invite;
  bool host, sending;
  std::uint64_t revision;
  int error_label;
  AE_REFLECT_MEMBERS(name,draft,invite,host,sending,revision,error_label)
};
struct Layout {
  int width,height,minimum_width,minimum_height;
  int margin,gap,label_width,button_width,body_top,input_height;
  AE_REFLECT_MEMBERS(width,height,minimum_width,minimum_height,margin,gap,
                     label_width,button_width,body_top,input_height)
};
class ChatUi;
class ChatControlPresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::gui::ChatControlPresenter",
      ChatControlPresenter, Presenter, 0)
 protected: ChatControlPresenter() = default;
 public:
  explicit ChatControlPresenter(ae::ObjProp p):Presenter{p}{}
  ae::ObjPtr<ChatUi> model;
  unsigned control;
  AE_OBJECT_REFLECT(AE_MMBR(model),AE_MMBR(control))
  template<class D> void Load(ae::Version<0>,D& d){ d(base_,model,control); }
  template<class D> void Save(ae::Version<0>,D& d)const{ d(base_,model,control); }
  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;
};
class ChatUiEvent;
class ChatUi : public NodeFor<ChatUi> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::gui::ChatUi",ChatUi,Node,0)
 protected: ChatUi() = default;
 public:
  explicit ChatUi(ae::ObjProp p):NodeFor{p}{}
  std::vector<ui::UiLocalizationString::ptr> labels;
  std::vector<ChatControlPresenter::ptr> presenters;
  std::vector<ControlState> controls;
  ConnectionState connection;
  EditingState editing;
  Layout layout;
  AE_OBJECT_REFLECT(AE_MMBR(labels),AE_MMBR(presenters),AE_MMBR(controls),
                    AE_MMBR(connection),AE_MMBR(editing),AE_MMBR(layout))
  template<class D> void Load(ae::Version<0>,D& d){
    d(base_,labels,presenters,controls,connection,editing,layout);
  }
  template<class D> void Save(ae::Version<0>,D& d)const{
    d(base_,labels,presenters,controls,connection,editing,layout);
  }
  std::string Text(Label label) const;
  std::string Title() const;
  ControlState const& Get(Control control) const;
  void EditDraft(std::string text);
  void EditInvite(std::string text);
  void Send();
  void Join();
  void Resize(int width,int height);
  void ClipboardResult(bool success);
  void Configure(bool host,std::string name,std::string initial_host);
  void AcceptTransport(ChatView view);
  void CompleteSend(std::uint64_t revision,bool committed);
  void Apply(ChatUiEvent const& event);
  void RefreshControls();
  // Runtime capabilities, bound only on the authoritative model thread.
  std::function<void(std::string,std::uint64_t)> send;
  std::function<void(std::string)> join;
  std::function<void()> create;
 private:
  bool initial_action_submitted_{false};
};
enum class Action : unsigned { Draft, Invite, SendStarted, SendCompleted,
  Transport, Configure, Resize, Feedback };
class ChatUiEvent : public EventFor<ChatUi,ChatUiEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::gui::ChatUiEvent",ChatUiEvent,Event,0)
 protected: ChatUiEvent() = default;
 public:
  explicit ChatUiEvent(ae::ObjProp p):EventFor{p}{}
  Action action;
  std::string text, second;
  bool flag;
  std::uint64_t revision;
  int first_value,second_value;
  ConnectionState connection;
  AE_OBJECT_REFLECT(AE_MMBR(action),AE_MMBR(text),AE_MMBR(second),AE_MMBR(flag),
      AE_MMBR(revision),AE_MMBR(first_value),AE_MMBR(second_value),AE_MMBR(connection))
};
struct Presentation {
  virtual ~Presentation()=default;
  virtual void Present(Control control,ControlState const& state,ChatUi const& model)=0;
  virtual void Remove(Control control)=0;
};
inline ae::ObjId RootId(){ return ae::ObjId{9500}; }
void EnsureUiRegistration();
ChatUi::ptr BuildUi(ae::Domain& domain);
} // namespace apptraverse::chat::gui
