#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "apptraverse/chat_session.h"
#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/presenter.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_text_control.h"

namespace apptraverse::chat::gui {
enum class Control : unsigned {
  UidLabel,
  Uid,
  Copy,
  Invite,
  Join,
  PeopleLabel,
  People,
  MessagesLabel,
  Messages,
  Input,
  Send,
  Status,
  Count
};
enum class Label : unsigned {
  Title,
  MyId,
  HostId,
  Copy,
  Join,
  People,
  Messages,
  Send,
  Message,
  Done,
  Connecting,
  Online,
  Offline,
  Unknown,
  TooLong,
  Rejected,
  Copied,
  CopyFailed,
  Quit,
  Edit,
  Cut,
  Paste,
  SelectAll,
  Theme,
  ThemeSystem,
  ThemeLight,
  ThemeDark,
  ThemeSoftDark,
  Count
};
struct Rect {
  int x{}, y{}, width{}, height{};
  AE_REFLECT_MEMBERS(x, y, width, height)
};
struct ControlState {
  std::string text;
  std::vector<std::string> rows;
  Rect frame;
  bool enabled, visible, read_only;
  AE_REFLECT_MEMBERS(text, rows, enabled, visible, read_only)
};
struct ConnectionState {
  std::string uid, invite, status;
  std::vector<std::string> people, messages;
  bool ready, in_room, online;
  bool operator==(ConnectionState const&) const = default;
  AE_REFLECT_MEMBERS(uid, invite, status, people, messages, ready, in_room,
                     online)
};
struct EditingState {
  std::string name, draft, invite;
  bool host, sending, configured;
  std::uint64_t revision;
  int error_label;
  AE_REFLECT_MEMBERS(name, draft, invite, host, sending, configured, revision,
                     error_label)
};
struct Layout {
  int minimum_width, minimum_height;
  int margin, gap, label_width, button_width, body_top, input_height;
  AE_REFLECT_MEMBERS(minimum_width, minimum_height, margin, gap, label_width,
                     button_width, body_top, input_height)
};
class ChatUi;
struct UiFactories {
  std::function<ui::UiDesktopWindowPresenter::ptr(ae::Domain&)> create_window;
  std::function<ui::UiPushButtonPresenter::ptr(ae::Domain&)> create_button;
  std::function<ui::UiTextControlPresenter::ptr(ae::Domain&)> create_text;
  std::function<ui::UiListContainerPresenter::ptr(ae::Domain&)> create_list;
  std::function<void(ui::UiDeviceState&)> prepare_device;
};
UiFactories HeadlessUiFactories();
bool TryConfigureUiFactories(UiFactories factories);
void ConfigureUiFactories(UiFactories factories);
UiFactories const& PresentationFactories();
class ChatActionButton : public NodeFor<ChatActionButton, ui::UiPushButton> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::gui::ChatActionButton",
                           ChatActionButton, ui::UiPushButton, 0)
 protected:
  ChatActionButton() = default;

 public:
  explicit ChatActionButton(ae::ObjProp p) : NodeFor{p} {}
  ae::ObjPtr<ChatUi> owner;
  unsigned action{};
  bool shown{true};
  bool visible() const override { return shown; }
  std::uint32_t FocusControlAfterClick() const override;
  AE_OBJECT_REFLECT(AE_MMBR(owner), AE_MMBR(action), AE_MMBR(shown))
  template <class D>
  void Load(ae::Version<0>, D& d) {
    d(base_, owner, action, shown);
  }
  template <class D>
  void Save(ae::Version<0>, D& d) const {
    d(base_, owner, action, shown);
  }
  void CollectHistoricalCaptureLiveReferenceIds(
      std::set<ae::ObjId>& ids) const override {
    ids.insert(owner.id());
  }
  void HandlePushButtonClicked() override;
};
class ChatTextControl : public NodeFor<ChatTextControl, ui::UiTextControl> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::gui::ChatTextControl",
                           ChatTextControl, ui::UiTextControl, 0)
 protected:
  ChatTextControl() = default;

 public:
  explicit ChatTextControl(ae::ObjProp p) : NodeFor{p} {}
  ae::ObjPtr<ChatUi> owner;
  unsigned role{};
  AE_OBJECT_REFLECT(AE_MMBR(owner), AE_MMBR(role))
  template <class D>
  void Load(ae::Version<0>, D& d) {
    d(base_, owner, role);
  }
  template <class D>
  void Save(ae::Version<0>, D& d) const {
    d(base_, owner, role);
  }
  void CollectHistoricalCaptureLiveReferenceIds(
      std::set<ae::ObjId>& ids) const override {
    ids.insert(owner.id());
  }
  void HandleTextEdited() override;
  void HandleTextAccepted() override;
};
class ChatWindowPresenter : public ui::UiDesktopWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::gui::ChatWindowPresenter",
                           ChatWindowPresenter, ui::UiDesktopWindowPresenter, 0)
 protected:
  ChatWindowPresenter() = default;

 public:
  explicit ChatWindowPresenter(ae::ObjProp p) : UiDesktopWindowPresenter{p} {}
  AE_OBJECT_REFLECT()
  template <class D>
  void Load(ae::Version<0>, D& d) {
    d(base_);
  }
  template <class D>
  void Save(ae::Version<0>, D& d) const {
    d(base_);
  }
  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;
};
class ChatControlPresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::gui::ChatControlPresenter",
                           ChatControlPresenter, Presenter, 0)
 protected:
  ChatControlPresenter() = default;

 public:
  explicit ChatControlPresenter(ae::ObjProp p) : Presenter{p} {}
  ae::ObjPtr<ChatUi> model;
  unsigned control;
  AE_OBJECT_REFLECT(AE_MMBR(model), AE_MMBR(control))
  template <class D>
  void Load(ae::Version<0>, D& d) {
    d(base_, model, control);
  }
  template <class D>
  void Save(ae::Version<0>, D& d) const {
    d(base_, model, control);
  }
  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;
  bool ReadyForPresentation() const override;
};
class ChatUiEvent;
class ChatUi : public NodeFor<ChatUi, ui::UiDesktopWindow> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::gui::ChatUi", ChatUi,
                           ui::UiDesktopWindow, 0)
 protected:
  ChatUi() = default;
  void OnClientSizeChanged() override;

 public:
  explicit ChatUi(ae::ObjProp p) : NodeFor{p} {}
  std::vector<ui::UiLocalizationString::ptr> labels;
  std::vector<ChatControlPresenter::ptr> presenters;
  std::vector<ControlState> controls;
  ConnectionState connection;
  EditingState editing;
  // Legacy Apple adapter descriptions remain source compatible. Native
  // non-Apple presentations use the standard control graph below.
  Layout layout;
  std::vector<ChatTextControl::ptr> text_controls;
  std::vector<ChatActionButton::ptr> action_buttons;
  std::vector<Presenter::ptr> native_presenters;
  ui::UiListContainer::ptr participants;
  std::uint64_t copy_request{};
  AE_OBJECT_REFLECT(AE_MMBR(labels), AE_MMBR(presenters), AE_MMBR(controls),
                    AE_MMBR(connection), AE_MMBR(editing),
                    AE_MMBR(text_controls), AE_MMBR(action_buttons),
                    AE_MMBR(native_presenters), AE_MMBR(participants),
                    AE_MMBR(copy_request))
  template <class D>
  void Load(ae::Version<0>, D& d) {
    d(base_, labels, presenters, controls, connection, editing, text_controls,
      action_buttons, native_presenters, participants, copy_request);
  }
  template <class D>
  void Save(ae::Version<0>, D& d) const {
    d(base_, labels, presenters, controls, connection, editing, text_controls,
      action_buttons, native_presenters, participants, copy_request);
  }
  std::string Text(Label label) const;
  std::string Title() const;
  ControlState const& Get(Control control) const;
  void EditDraft(std::string text);
  void EditInvite(std::string text);
  void Send();
  void Join();
  void Copy();
  void SelectTheme(unsigned choice);
  void Resize(int width, int height);
  void ClipboardResult(bool success);
  void Configure(bool host, std::string name, std::string initial_host);
  void AcceptTransport(ChatView view);
  void CompleteSend(std::uint64_t revision, bool committed);
  void RecoverInterruptedSend();
  void Apply(ChatUiEvent const& event);
  void RefreshControls();
  // Runtime capabilities, bound only on the authoritative model thread.
  std::uint64_t next_send_id{};
  std::map<std::uint64_t, std::string> pending_sends;
  std::function<void(std::string, std::uint64_t)> send;
  std::function<void(std::string)> join;
  std::function<void()> create;

 private:
  bool initial_action_submitted_{false};
};
enum class Action : unsigned {
  Draft,
  Invite,
  SendStarted,
  SendCompleted,
  Transport,
  Configure,
  Feedback,
  Copy
};
class ChatUiEvent : public EventFor<ChatUi, ChatUiEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::gui::ChatUiEvent", ChatUiEvent,
                           Event, 0)
 protected:
  ChatUiEvent() = default;

 public:
  explicit ChatUiEvent(ae::ObjProp p) : EventFor{p} {}
  Action action;
  std::string text, second;
  bool flag;
  std::uint64_t revision;
  int first_value;
  ConnectionState connection;
  AE_OBJECT_REFLECT(AE_MMBR(action), AE_MMBR(text), AE_MMBR(second),
                    AE_MMBR(flag), AE_MMBR(revision), AE_MMBR(first_value),
                    AE_MMBR(connection))
};
struct Presentation {
  virtual ~Presentation() = default;
  virtual void Present(Control control, ControlState const& state,
                       ChatUi const& model) = 0;
  virtual void Remove(Control control) = 0;
  virtual void PresentWindow(ChatUi const& model) = 0;
  virtual void RemoveWindow() = 0;
};
inline ae::ObjId RootId() { return ae::ObjId{9500}; }
void EnsureUiRegistration();
ChatUi::ptr BuildUi(ae::Domain& domain);
}  // namespace apptraverse::chat::gui
