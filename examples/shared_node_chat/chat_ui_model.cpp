#include "chat_ui_model.h"
#include <algorithm>
#include <cassert>
#include <mutex>
#include <optional>
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_presented_window_composition.h"
#include "chat_controls.h"

namespace apptraverse::chat::gui {
APPTRAVERSE_REGISTER(ChatUi);
APPTRAVERSE_REGISTER(ChatActionButton);
APPTRAVERSE_REGISTER(ChatTextControl);
APPTRAVERSE_REGISTER(ChatUiEvent);
APPTRAVERSE_REGISTER(ChatControlPresenter);
APPTRAVERSE_REGISTER(ChatWindowPresenter);
void EnsureUiRegistration() {
  static std::once_flag once;
  std::call_once(once, [] {
    EnsureObjectRegistration();
    ui::EnsureUiControlRegistration();
    (void)&g_apptraverse_registrar_ChatActionButton;
    (void)&g_apptraverse_registrar_ChatTextControl;
    (void)&g_apptraverse_registrar_ChatUi;
    (void)&g_apptraverse_registrar_ChatUiEvent;
    (void)&g_apptraverse_registrar_ChatControlPresenter;
    (void)&g_apptraverse_registrar_ChatWindowPresenter;
  });
}
namespace {
std::optional<UiFactories> factories;
}
UiFactories HeadlessUiFactories() {
  UiFactories value;
  value.create_window = [](ae::Domain& d) {
    return ui::UiDesktopWindowPresenter::ptr::Create(ae::CreateWith{d});
  };
  value.create_button = [](ae::Domain& d) {
    return ui::UiPushButtonPresenter::ptr::Create(ae::CreateWith{d});
  };
  value.create_text = [](ae::Domain& d) {
    return ui::UiTextControlPresenter::ptr::Create(ae::CreateWith{d});
  };
  value.create_list = [](ae::Domain& d) {
    return ui::UiListContainerPresenter::ptr::Create(ae::CreateWith{d});
  };
  value.prepare_device = [](ui::UiDeviceState&) {};
  return value;
}
bool TryConfigureUiFactories(UiFactories value) {
  if (!value.create_window || !value.create_button || !value.create_text ||
      !value.create_list || !value.prepare_device)
    return false;
  factories = std::move(value);
  return true;
}
void ConfigureUiFactories(UiFactories value) {
  if (!TryConfigureUiFactories(std::move(value))) {
    WriteFatalStderr("incomplete chat UI composition\n");
    std::abort();
  }
}
UiFactories const& PresentationFactories() {
  if (!factories) {
    WriteFatalStderr("chat UI composition must be configured explicitly\n");
    std::abort();
  }
  return *factories;
}
std::uint32_t ChatActionButton::FocusControlAfterClick() const {
  return static_cast<Control>(action) == Control::Send ? kInputControl : 0;
}
void ChatActionButton::HandlePushButtonClicked() {
  switch (static_cast<Control>(action)) {
    case Control::Copy:
      owner->Copy();
      break;
    case Control::Join:
      owner->Join();
      break;
    case Control::Send:
      owner->Send();
      break;
    default:
      break;
  }
}
void ChatTextControl::HandleTextEdited() {
  if (static_cast<Control>(role) == Control::Input)
    owner->EditDraft(text);
  else if (static_cast<Control>(role) == Control::Invite)
    owner->EditInvite(text);
}
void ChatTextControl::HandleTextAccepted() {
  if (static_cast<Control>(role) == Control::Input)
    owner->Send();
  else if (static_cast<Control>(role) == Control::Invite)
    owner->Join();
}
void ChatWindowPresenter::OnLoad() { OnModelChanged(); }
void ChatWindowPresenter::OnModelChanged() {
  assert(presentation_host && model_proxy);
  static_cast<Presentation*>(presentation_host)
      ->PresentWindow(static_cast<ChatUi const&>(*window));
}
void ChatWindowPresenter::OnUnload() {
  static_cast<Presentation*>(presentation_host)->RemoveWindow();
}
bool ChatControlPresenter::ReadyForPresentation() const {
  return model->presenter->presentation_loaded;
}
void ChatControlPresenter::OnLoad() { OnModelChanged(); }
void ChatControlPresenter::OnModelChanged() {
  assert(presentation_host && model_proxy);
  static_cast<Presentation*>(presentation_host)
      ->Present(static_cast<Control>(control), model->controls.at(control),
                *model);
}
void ChatControlPresenter::OnUnload() {
  static_cast<Presentation*>(presentation_host)
      ->Remove(static_cast<Control>(control));
}
std::string ChatUi::Text(Label label) const {
  return labels.at(static_cast<unsigned>(label))
      ->Resolve(presenter->locale->language_index());
}
void ChatUi::RequireLocalizedLabels() const {
  constexpr auto count = static_cast<unsigned>(Label::Count) +
                         static_cast<unsigned>(ChatStatus::Count) - 1;
  if (labels.size() != count ||
      std::any_of(labels.begin(), labels.end(),
                  [](auto const& label) { return !label.is_valid(); })) {
    WriteFatalStderr("incomplete chat localization supply\n");
    std::abort();
  }
}
std::string ChatUi::Title() const {
  return Text(Label::Title) + " - " + editing.name;
}
void ChatUi::PrepareLanguage(ui::UiLanguage language) {
  auto const index = static_cast<int>(language);
  for (auto& label : labels) {
    label.Load();
    label->MaterializeLanguage(index);
  }
  presenter->title->MaterializeLanguage(index);
  presenter->locale->SubmitLanguageIndexFromUi(index);
}
ControlState const& ChatUi::Get(Control c) const {
  return controls.at(static_cast<unsigned>(c));
}
namespace {
ChatUiEvent::ptr Make(ChatUi& model, Action action) {
  auto event = ChatUiEvent::ptr::Create(ae::CreateWith{*model.domain});
  event->action = action;
  event->text.clear();
  event->second.clear();
  event->flag = false;
  event->revision = 0;
  event->first_value = 0;
  event->connection = {};
  return event;
}
}  // namespace
void ChatUi::EditDraft(std::string text) {
  auto e = Make(*this, Action::Draft);
  e->text = std::move(text);
  Commit(e);
}
void ChatUi::EditInvite(std::string text) {
  auto e = Make(*this, Action::Invite);
  e->text = std::move(text);
  Commit(e);
}
void ChatUi::Resize(int width, int height) {
  if (width <= 0 || height <= 0)
    return;  // Native surface temporarily unavailable.
  // Legacy native adapters may report viewport sizes; geometry stays local.
  (void)width;
  (void)height;
}
void ChatUi::OnClientSizeChanged() {}
void ChatUi::ClipboardResult(bool ok) {
  auto e = Make(*this, Action::Feedback);
  e->first_value = static_cast<int>(ok ? Label::Copied : Label::CopyFailed);
  Commit(e);
}
void ChatUi::Configure(bool host, std::string name, std::string initial_host) {
  if (editing.configured) return;
  auto e = Make(*this, Action::Configure);
  e->flag = host;
  e->text = std::move(name);
  e->second = std::move(initial_host);
  Commit(e);
  auto title_event =
      ui::SetUiLocalizationSlotUtf8Event::ptr::Create(ae::CreateWith{*domain});
  title_event->slot_index = presenter->locale->language_index();
  title_event->utf8 = Title();
  presenter->title->Commit(title_event);
  auto root = layout_state->definition->root;
  auto& slots = root.children.front().children;
  if (host)
    slots.erase(slots.begin() + 3, slots.end());
  else
    slots.erase(slots.begin() + 1, slots.begin() + 3);
  layout_state->SetOverride(std::move(root));
}
void ChatUi::Copy() {
  auto e = Make(*this, Action::Copy);
  Commit(e);
}
void ChatUi::Join() {
  if (connection.ready && !connection.in_room && !editing.host)
    join(editing.invite);
}
void ChatUi::Send() {
  if (!connection.ready || !connection.in_room ||
      editing.draft.find_first_not_of(" \t\r\n") == std::string::npos)
    return;
  if (editing.draft.size() > 8192) {
    auto e = Make(*this, Action::Feedback);
    e->first_value = static_cast<int>(Label::TooLong);
    Commit(e);
    return;
  }
  auto text = editing.draft;
  auto revision = ++next_send_id;
  // Apply the draft change before submission. Fold the UI events once the
  // transport reports the result, outside the path to its send queue.
  if (pending_sends.empty() && HasImmediateEventCompaction()) {
    compact_after_send_ = true;
    SetImmediateEventCompaction(false);
  }
  pending_sends.emplace(revision, text);
  Commit(Make(*this, Action::SendStarted));
  send(std::move(text), revision);
}
void ChatUi::CompleteSend(std::uint64_t revision, bool committed) {
  auto e = Make(*this, Action::SendCompleted);
  e->revision = revision;
  e->flag = committed;
  auto pending = pending_sends.find(revision);
  if (pending != pending_sends.end()) {
    if (!committed) e->text = pending->second;
    pending_sends.erase(pending);
  }
  e->first_value = static_cast<int>(pending_sends.size());
  Commit(e);
  if (compact_after_send_ && pending_sends.empty()) {
    compact_after_send_ = false;
    SetImmediateEventCompaction(true);
  }
}
void ChatUi::RecoverInterruptedSend() {
  if (!editing.sending) return;
  // Legacy recovery for an in-memory graph; Session always starts fresh.
  // Never automatically resend an uncertain submission.
  CompleteSend(editing.revision, false);
  auto event = Make(*this, Action::Feedback);
  event->first_value = static_cast<int>(Label::Unknown);
  Commit(event);
}
ChatTransportUpdate::ChatTransportUpdate(ChatView view) : status{view.status_code} {
  state.uid = std::move(view.local_uid);
  state.invite = std::move(view.invite);
  state.messages = std::move(view.messages);
  state.ready = view.ready;
  state.in_room = view.in_room;
  state.online = view.locally_online;
  for (auto& participant : view.participants)
    state.people.push_back(std::move(participant.name));
}
void ChatUi::AcceptTransport(ChatTransportUpdate update) {
  auto& next = update.state;
  if (update.status != ChatStatus::Unspecified) {
    auto const label_count = static_cast<unsigned>(Label::Count);
    next.status =
        labels.at(label_count + static_cast<unsigned>(update.status) - 1)
            ->Resolve(presenter->locale->language_index());
  }
  if (next != connection) {
    if (next.people != connection.people) {
      auto replacement =
          ui::ReplaceUiListRowsEvent::ptr::Create(ae::CreateWith{*domain});
      replacement->selection_index = -1;
      for (auto const& name : next.people) {
        auto row = ui::UiListRow::ptr::Create(ae::CreateWith{*domain});
        auto label =
            ui::UiLocalizationString::ptr::Create(ae::CreateWith{*domain});
        auto text = ui::UiString::ptr::Create(ae::CreateWith{*domain});
        text->utf8 = name;
        label->strings.assign(ui::UiLanguageCount(), text);
        row->primary_text = label;
        InitializeRuntimeNode(*label, *this);
        InitializeRuntimeNode(*row, *this);
        replacement->rows.push_back(row);
      }
      participants->Commit(replacement);
    }
    auto e = Make(*this, Action::Transport);
    e->connection = std::move(next);
    Commit(e);
  }
  if (connection.ready && !initial_action_submitted_) {
    initial_action_submitted_ = true;
    if (!connection.in_room) {
      if (editing.host)
        create();
      else if (!editing.invite.empty())
        join(editing.invite);
    }
  }
}
void ChatUi::Apply(ChatUiEvent const& e) {
  switch (e.action) {
    case Action::Draft:
      editing.draft = e.text;
      ++editing.revision;
      editing.error_label = -1;
      break;
    case Action::Invite:
      editing.invite = e.text;
      ++editing.revision;
      editing.error_label = -1;
      break;
    case Action::SendStarted:
      editing.sending = true;
      editing.draft.clear();
      editing.error_label = -1;
      break;
    case Action::SendCompleted:
      editing.sending = e.first_value != 0;
      if (!e.flag) {
        if (!e.text.empty()) {
          if (!editing.draft.empty()) editing.draft += "\n";
          editing.draft += e.text;
        }
        editing.error_label = static_cast<int>(Label::Rejected);
      }
      break;
    case Action::Transport:
      connection = e.connection;
      if (!connection.invite.empty()) editing.invite = connection.invite;
      break;
    case Action::Configure:
      editing.host = e.flag;
      editing.name = e.text;
      if (!e.second.empty()) editing.invite = e.second;
      editing.configured = true;
      break;
    case Action::Feedback:
      editing.error_label = e.first_value;
      break;
    case Action::Copy:
      ++copy_request;
      break;
  }
  RefreshControls();
  NoteMaterializedChange();
}
void ChatUi::RefreshControls() {
  auto& values = controls;
  auto at = [&](Control c) -> ControlState& {
    return values.at(static_cast<unsigned>(c));
  };
  for (auto& c : values) {
    c.enabled = true;
    c.visible = true;
    c.read_only = false;
  }
  at(Control::UidLabel).text = Text(editing.host ? Label::MyId : Label::HostId);
  at(Control::Uid).text =
      connection.uid.empty() ? Text(Label::Connecting) : connection.uid;
  at(Control::Uid).read_only = true;
  at(Control::Uid).visible = editing.host;
  at(Control::Copy).text = Text(Label::Copy);
  at(Control::Copy).visible = editing.host;
  at(Control::Copy).enabled = connection.ready && !connection.uid.empty();
  at(Control::Invite).text = editing.invite;
  at(Control::Invite).visible = !editing.host;
  at(Control::Invite).read_only = connection.in_room;
  at(Control::Join).text = Text(Label::Join);
  at(Control::Join).visible = !editing.host;
  at(Control::Join).enabled = connection.ready && !connection.in_room;
  at(Control::PeopleLabel).text = Text(Label::People);
  at(Control::People).rows = connection.people;
  at(Control::MessagesLabel).text = Text(Label::Messages);
  at(Control::Messages).rows = connection.messages;
  at(Control::Messages).read_only = true;
  at(Control::Input).text = editing.draft;
  at(Control::Input).enabled = connection.ready && connection.in_room;
  at(Control::Send).text = Text(Label::Send);
  at(Control::Send).enabled = connection.ready && connection.in_room;
  auto& status = at(Control::Status).text;
  if (editing.error_label >= 0) {
    status = Text(static_cast<Label>(editing.error_label));
  } else {
    status = Text(connection.online ? Label::Online : Label::Offline);
    if (!connection.status.empty()) {
      status += " | " + connection.status;
    }
  }
  for (auto const& control : text_controls) {
    auto const& state = Get(static_cast<Control>(control->role));
    control->Materialize(state.text, state.rows, state.enabled, state.visible,
                         state.read_only);
    auto role = static_cast<Control>(control->role);
    control->MaterializeAccessibleLabel(
        role == Control::Input      ? Text(Label::Message)
        : role == Control::Uid      ? Text(Label::MyId)
        : role == Control::Invite   ? Text(Label::HostId)
        : role == Control::Messages ? Text(Label::Messages)
                                    : std::string{});
  }
  for (auto const& button : action_buttons) {
    auto const& state = Get(static_cast<Control>(button->action));
    button->MaterializeEnabled(state.enabled);
    button->shown = state.visible;
  }
}

#ifdef APPTRAVERSE_CHAT_UI_PRODUCER
ChatUi::ptr BuildUi(ae::Domain& domain) {
  ae::Domain::ObjectIdSequence ids{domain, ae::ObjId{9501}, ae::ObjId{9999}};
  auto app = ChatUi::ptr::Create(ae::CreateWith{domain}.with_id(RootId()));
  app->connection = {};
  app->editing = {};
  app->editing.error_label = -1;
  app->copy_request = 0;
  struct Translation {
    char const* en;
    char const* ru;
  };
  constexpr Translation captions[] = {
      {"SharedNode Chat", "Чат SharedNode"},
      {"My Aether ID", "Мой Aether ID"},
      {"Host Aether ID", "Aether ID хоста"},
      {"Copy", "Копировать"},
      {"Join", "Войти"},
      {"Participants", "Участники"},
      {"Messages", "Сообщения"},
      {"Send", "Отправить"},
      {"Message", "Сообщение"},
      {"Done", "Готово"},
      {"Connecting...", "Подключение…"},
      {"Online", "В сети"},
      {"Offline", "Не в сети"},
      {"Unknown", "Неизвестно"},
      {"Message is too long (maximum 8192 UTF-8 bytes).",
       "Слишком длинное сообщение (до 8192 байт UTF-8)."},
      {"Could not save the message.", "Не удалось сохранить сообщение."},
      {"Aether ID copied", "Aether ID скопирован"},
      {"Could not copy Aether ID", "Не удалось скопировать Aether ID"},
      {"Quit", "Выход"},
      {"Edit", "Правка"},
      {"Cut", "Вырезать"},
      {"Paste", "Вставить"},
      {"Select All", "Выделить всё"},
  };
  constexpr char const* status_ru[] = {
      "",
      "Запуск Aether…",
      "Соединение установлено",
      "Ожидание хоста…",
      "Сеанс восстановлен",
      "Введите Aether ID хоста",
      "Не удалось создать комнату в памяти",
      "Комната создана. Передайте участникам свой Aether ID",
      "Некорректный Aether ID хоста",
      "Поиск комнаты хоста…",
      "Не удалось выбрать хост",
      "Слишком длинное сообщение (до 8192 байт)",
      "Этот клиент не входит в комнату",
      "Не удалось добавить сообщение",
      "Не удалось сохранить снимок для вернувшегося участника",
      "Не удалось добавить участника",
      "Не удалось открыть профиль чата",
      "Слишком длинное имя (до 256 байт)",
      "Не удалось создать профиль чата",
      "Подключение…",
      "Не удалось установить соединение",
      "Не удалось сохранить идентификатор Aether",
      "Не удалось открыть чат",
      "Не удалось сохранить состояние доставки",
      "Не удалось сохранить состояние SharedNode",
      "Остановлено",
      "Не удалось зафиксировать состояние чата в памяти",
  };
  static_assert(std::size(captions) == static_cast<unsigned>(Label::Count));
  static_assert(std::size(status_ru) ==
                static_cast<unsigned>(ChatStatus::Count));
  auto make_text = [&](char const* utf8) {
    auto value = ui::UiString::ptr::Create(ae::CreateWith{domain});
    value->utf8 = utf8;
    value->SetObjectScope({{}, {}, true});
    return value;
  };
  auto add_label = [&](char const* english) {
    auto label = ui::UiLocalizationString::ptr::Create(ae::CreateWith{domain});
    label->strings = {make_text(english)};
    label->SetObjectScope({{}, {}, true});
    app->labels.push_back(label);
  };
  for (auto const& caption : captions) add_label(caption.en);
  // The original supply allocated 28 caption pairs before the window. Reuse
  // its five former menu slots so existing LOCAL window/device IDs stay valid.
  constexpr unsigned first_status_count = 5;
  for (unsigned i = 1; i <= first_status_count; ++i)
    add_label(ChatStatusText(static_cast<ChatStatus>(i)));
  auto locale = ui::UiLocaleSettings::ptr::Create(ae::CreateWith{domain});
  locale->BootstrapLanguageIndex(0);
  auto const& factories = PresentationFactories();
  auto window = factories.create_window(domain);
  auto definition = ui::UiLayoutDefinition::ptr::Create(ae::CreateWith{domain});
  definition->SetObjectScope({{}, {}, true});
  auto leaf = [](unsigned id, double grow = 0, double width = 0,
                 double height = 0) {
    ui::UiLayoutNode n;
    n.control_id = id;
    n.grow = grow;
    n.min_width = width;
    n.min_height = height;
    return n;
  };
  auto stack = [](ui::UiLayoutFlow flow, std::vector<ui::UiLayoutNode> children,
                  double grow = 0) {
    ui::UiLayoutNode n;
    n.flow = flow;
    n.gap = 8;
    n.grow = grow;
    n.children = std::move(children);
    return n;
  };
  auto top = stack(ui::UiLayoutFlow::kHorizontal,
                   {leaf(110), leaf(kUidControl, 1, 100), leaf(kHostControl),
                    leaf(kInviteControl, 1, 100), leaf(kJoinControl)});
  auto people = stack(ui::UiLayoutFlow::kVertical,
                      {leaf(111), leaf(kParticipantsControl, 1, 120, 100)}, 1);
  auto history =
      stack(ui::UiLayoutFlow::kVertical,
            {leaf(112), leaf(kMessagesControl, 1, 180, 100),
             stack(ui::UiLayoutFlow::kHorizontal,
                   {leaf(kInputControl, 1, 100), leaf(kSendControl)})},
            3);
  definition->root =
      stack(ui::UiLayoutFlow::kVertical,
            {top, stack(ui::UiLayoutFlow::kHorizontal, {people, history}, 1),
             leaf(kStatusControl)});
  definition->root.padding = 12;
  auto layout_state = ui::UiLayoutState::ptr::Create(ae::CreateWith{domain});
  layout_state->Bootstrap(definition, false, {});
  auto device = ui::UiDeviceState::ptr::Create(ae::CreateWith{domain});
  device->Bootstrap({}, {});
  auto title = ui::UiLocalizationString::ptr::Create(ae::CreateWith{domain});
  auto title_text = ui::UiString::ptr::Create(ae::CreateWith{domain});
  title_text->utf8 = "SharedNode Chat";
  title->strings = {title_text};
  ui::ComposePresentedWindow(app, window, title, locale, layout_state, device);
  app->BootstrapFrame(0, 0, 840, 560);
  app->layout = {400, 300, 12, 8, 100, 100, 70, 28};
  app->controls.resize(static_cast<unsigned>(Control::Count));
  for (auto& control : app->controls) control = {};
  struct TextDefinition {
    Control role;
    unsigned id;
    ui::UiTextControlKind kind;
  };
  TextDefinition const texts_to_create[] = {
      {Control::UidLabel, 110, ui::UiTextControlKind::kLabel},
      {Control::Uid, kUidControl, ui::UiTextControlKind::kEdit},
      {Control::Invite, kInviteControl, ui::UiTextControlKind::kEdit},
      {Control::PeopleLabel, 111, ui::UiTextControlKind::kLabel},
      {Control::MessagesLabel, 112, ui::UiTextControlKind::kLabel},
      {Control::Messages, kMessagesControl, ui::UiTextControlKind::kMultiline},
      {Control::Input, kInputControl, ui::UiTextControlKind::kEdit},
      {Control::Status, kStatusControl, ui::UiTextControlKind::kLabel}};
  for (auto const& entry : texts_to_create) {
    auto control = ChatTextControl::ptr::Create(ae::CreateWith{domain});
    control->text.clear();
    control->accessible_label.clear();
    control->rows.clear();
    control->enabled = true;
    control->visible = true;
    control->read_only = false;
    control->revision = 0;
    control->owner = app;
    control->role = static_cast<unsigned>(entry.role);
    control->kind = entry.kind;
    app->text_controls.push_back(control);
    auto presenter = factories.create_text(domain);
    presenter->control = control;
    presenter->window_presenter = window;
    presenter->control_id = entry.id;
    app->native_presenters.push_back(presenter);
  }
  struct ButtonDefinition {
    Control role;
    unsigned id;
    Label label;
  };
  for (auto const& entry :
       {ButtonDefinition{Control::Copy, kHostControl, Label::Copy},
        ButtonDefinition{Control::Join, kJoinControl, Label::Join},
        ButtonDefinition{Control::Send, kSendControl, Label::Send}}) {
    auto button = ChatActionButton::ptr::Create(ae::CreateWith{domain});
    button->shown = true;
    button->owner = app;
    button->action = static_cast<unsigned>(entry.role);
    button->label = app->labels.at(static_cast<unsigned>(entry.label));
    button->BootstrapSetEnabled(false);
    app->action_buttons.push_back(button);
    auto presenter = factories.create_button(domain);
    presenter->button = button;
    presenter->window_presenter = window;
    presenter->control_id = entry.id;
    presenter->layout_x = 0;
    presenter->layout_y = 0;
    presenter->layout_width = 0;
    presenter->layout_height = 0;
    app->native_presenters.push_back(presenter);
  }
  app->participants = ui::UiListContainer::ptr::Create(ae::CreateWith{domain});
  app->participants->selection_index = -1;
  app->participants->rows.clear();
  auto list = factories.create_list(domain);
  list->container = app->participants;
  list->window_presenter = window;
  list->control_id = kParticipantsControl;
  list->notify_list_selection_change = false;
  list->layout_x = 0;
  list->layout_y = 0;
  list->layout_width = 0;
  list->layout_height = 0;
  app->native_presenters.push_back(list);
  for (unsigned i = first_status_count + 1; i < std::size(status_ru); ++i)
    add_label(ChatStatusText(static_cast<ChatStatus>(i)));
  for (unsigned i = 0; i < std::size(captions); ++i)
    app->labels[i]->strings.push_back(make_text(captions[i].ru));
  for (unsigned i = 1; i < std::size(status_ru); ++i)
    app->labels[std::size(captions) + i - 1]->strings.push_back(
        make_text(status_ru[i]));
  // The title slots are mutable per instance; never alias a constant caption.
  auto russian_title = ui::UiString::ptr::Create(ae::CreateWith{domain});
  russian_title->utf8 = captions[static_cast<unsigned>(Label::Title)].ru;
  title->strings.push_back(russian_title);
  app->RequireLocalizedLabels();
  app->RefreshControls();
  return app;
}
#endif
}  // namespace apptraverse::chat::gui
