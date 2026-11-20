#include "chat_ui_model.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include "apptraverse/application_ui_session.h"
#include "apptraverse/distill.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/single_file_user_storage.h"
#ifdef _WIN32
#  include "apptraverse/platform/windows/win_ui_presenters.h"
#  include "chat_windows_presenter.h"
#  include "generated/windows/chat_ui_supply.h"
#elif defined(__APPLE__)
#  include "generated/apple/chat_ui_supply.h"
#else
#  include "generated/chat_ui_supply.h"
#endif
using namespace apptraverse;
using namespace apptraverse::chat::gui;
#define CHECK(x)                                             \
  do {                                                       \
    if (!(x)) {                                              \
      std::cerr << "check " #x " line " << __LINE__ << '\n'; \
      std::abort();                                          \
    }                                                        \
  } while (false)
int main() {
  EnableNoninteractiveCrt();
  EnsureUiRegistration();
  ConfigureUiFactories(HeadlessUiFactories());
  auto incomplete = HeadlessUiFactories();
  incomplete.create_text = {};
  CHECK(!TryConfigureUiFactories(std::move(incomplete)));
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto app = BuildUi(domain);
  FinalizeDistilledGraph(*app);
  app->SetImmediateEventCompaction(true);
  app->device_state->SetAppearance({ui::UiThemeMode::kDark, true});
  CHECK(app->device_state->appearance.theme == ui::UiThemeMode::kDark &&
        app->device_state->appearance.soften_dark);
  app->device_state->SetAppearance({ui::UiThemeMode::kAuto, false});
  CHECK(app->device_state->appearance.theme == ui::UiThemeMode::kAuto &&
        !app->device_state->appearance.soften_dark);
  app->Configure(true, "Host", "");
  app->Configure(false, "Replacement", "unexpected-host");
  CHECK(app->editing.host && app->editing.name == "Host" &&
        app->editing.invite.empty());
  auto const initial_edit = app->editing.revision;
  app->EditInvite("first invite");
  app->EditInvite("latest invite");
  CHECK(app->editing.revision == initial_edit + 2 &&
        app->editing.invite == "latest invite");
  unsigned creates = 0, sends = 0;
  std::uint64_t revision = 0;
  app->create = [&] { ++creates; };
  app->join = [](std::string) { CHECK(false); };
  app->send = [&](std::string text, std::uint64_t r) {
    CHECK(text == "first");
    ++sends;
    revision = r;
  };
  chat::ChatView network;
  network.ready = true;
  app->AcceptTransport(network);
  CHECK(creates == 1);
  network.in_room = true;
  app->AcceptTransport(network);
  auto input_control = app->text_controls.at(6);
  input_control->SetImmediateEventCompaction(true);
  std::uint64_t input_sequence = 0;
  for (auto text : {"a", "ab", "abc"}) {
    input_control->SubmitText(text, ++input_sequence);
    CHECK(app->editing.draft == text &&
          input_control->revision == input_sequence);
    CHECK(input_control->owner.operator->() == &*app);
    CHECK(input_control->owner.operator->() ==
          domain.Find(RootId()).as<ChatUi>());
    CHECK(input_control->owner.id() == RootId() &&
          input_control->role == static_cast<unsigned>(Control::Input));
  }
  app->EditDraft("");
  auto send_button = app->action_buttons.at(2);
  send_button->SetImmediateEventCompaction(true);
  send_button->NotifyClicked();
  CHECK(send_button->owner.operator->() == &*app);
  CHECK(send_button->owner.operator->() == domain.Find(RootId()).as<ChatUi>());
  unsigned transport_changes = 0;
  app->BindMaterializedChangeNotifier(
      &transport_changes,
      [](void* count, Node&) { ++*static_cast<unsigned*>(count); });
  // ACK/progress/path diagnostics must not republish the persistent UI graph.
  network.pending_packets = 2;
  network.retained_events = 3;
  network.tcp_path_verified = true;
  app->AcceptTransport(network);
  CHECK(transport_changes == 0);
  network.pending_packets = 0;
  network.retained_events = 0;
  app->AcceptTransport(network);
  CHECK(transport_changes == 0);
  network.messages = {"Remote: delivered"};
  app->AcceptTransport(network);
  CHECK(transport_changes > 0);
  CHECK(app->Get(Control::Messages).rows == network.messages);
  auto const published_changes = transport_changes;
  app->AcceptTransport(network);
  CHECK(transport_changes == published_changes);
  app->ClearMaterializedChangeNotifier();
  CHECK(creates == 1);
  app->EditDraft("first");
  app->Send();
  CHECK(sends == 1 && app->editing.sending);
  CHECK(app->Get(Control::Send).enabled && app->Get(Control::Input).enabled);
  app->Send();
  CHECK(sends == 1);  // A second click cannot enqueue the same draft.
  app->CompleteSend(revision, false);
  CHECK(app->editing.draft == "first");
  CHECK(!app->editing.sending && app->Get(Control::Send).enabled);
  CHECK(app->editing.error_label == static_cast<int>(Label::Rejected));
  app->Send();
  app->EditDraft("newer");
  app->CompleteSend(revision, true);
  CHECK(app->editing.draft == "newer" && !app->editing.sending);
  app->send = [&](std::string text, std::uint64_t r) {
    CHECK(text == "newer");
    revision = r;
  };
  app->Send();
  app->CompleteSend(revision, true);
  CHECK(app->editing.draft.empty());
  std::vector<std::uint64_t> queued;
  app->send = [&](std::string, std::uint64_t r) { queued.push_back(r); };
  app->EditDraft("one");
  app->Send();
  app->EditDraft("two");
  app->Send();
  CHECK(queued.size() == 2 && app->editing.draft.empty() &&
        app->Get(Control::Send).enabled);
  app->EditDraft("next");
  app->CompleteSend(queued[0], true);
  CHECK(app->editing.sending && app->editing.draft == "next");
  app->CompleteSend(queued[1], false);
  CHECK(!app->editing.sending && app->editing.draft == "next\ntwo");
  app->EditDraft(std::string(8193, 'x'));
  app->Send();
  CHECK(app->editing.error_label == static_cast<int>(Label::TooLong));
  app->EditDraft("retained");
  auto const semantic_width = app->client_width();
  auto const semantic_height = app->client_height();
  app->Resize(393, 156);
  CHECK(app->client_width() == semantic_width &&
        app->client_height() == semantic_height);
  CHECK(app->layout_state && app->device_state);
  // Local solver results adapt to the actual viewport without writing pixels
  // into the model or replacing required native controls.
  std::vector<ui::UiLayoutMeasurement> measurements;
  for (auto id :
       {110u, 101u, 102u, 103u, 104u, 111u, 105u, 112u, 106u, 107u, 108u, 109u})
    measurements.push_back({id, 20, 20, 50, 24});
  for (auto viewport : {std::pair{393, 156}, std::pair{840, 560}}) {
    auto result = ui::ComputeUiConstraintLayout(
        app->layout_state->EffectiveRoot(), viewport.first, viewport.second,
        measurements);
    if (!result.valid || result.controls.size() != 10)
      std::cerr << "layout valid=" << result.valid
                << " controls=" << result.controls.size()
                << " error=" << result.error << "\n";
    CHECK(result.valid && result.controls.size() == 10);
    for (auto const& frame : result.controls)
      CHECK(frame.width >= 0 && frame.height >= 0);
    if (viewport.second == 156) CHECK(result.overflow_y);
  }
  ByteSink publication;
  SerializeInitialPublication(*app, publication);
  ModelObjectProxy proxy{[](auto) {}};
  {
    ApplicationUiSession session{publication.bytes, nullptr, proxy};
    auto& mirror = static_cast<ChatUi&>(session.root());
    CHECK(&mirror != &*app && mirror.editing.draft == "retained");
    CHECK(mirror.presenter->presentation_loaded);
    CHECK(mirror.text_controls.size() == 8 &&
          mirror.action_buttons.size() == 3);
    CHECK(mirror.native_presenters.size() == 12);
    CHECK(mirror.text_controls.front()->owner.id() == mirror.obj_id);
    CHECK(mirror.device_state.id() == app->device_state.id());
  }
  // Native serialization is still used for publication/replay in RAM. It does
  // not imply that a chat business graph may be written to a profile file.
  SaveDistilledRoot(*app);
  std::vector<std::uint8_t> saved;
  CHECK(SerializeUserStorage(storage, saved));
  ae::RamDomainStorage cold;
  CHECK(DeserializeUserStorage(cold, saved));
  ae::Domain cold_domain{cold};
  auto restored = LoadApplication<ChatUi>(cold_domain, RootId());
  CHECK(restored->editing.draft == "retained");
  CHECK(restored->text_controls.size() == 8 &&
        restored->native_presenters.size() == 12);
  CHECK(restored->text_controls.front()->owner.id() == RootId());
#ifdef _WIN32
  ui::windows::EnsureWinControlPresenterRegistration();
  apptraverse::chat::EnsureChatWindowsPresenterRegistration();
#endif
  {
    // Consume the actual executable-emitted supply; no builder initializes this
    // graph.
    ae::RamDomainStorage embedded_user, embedded_backing;
    auto const& supply = embedded_distilled::shared_chat_ui::kEmbeddedGraph;
    InstallEmbeddedDistilledBacking(embedded_backing, supply);
    OverlayDomainStorage overlay{embedded_user, embedded_backing,
                                 EmbeddedSupplyConstantIdSet(supply)};
    ae::Domain consumer{overlay};
    auto loaded = LoadApplication<ChatUi>(consumer, RootId());
    CHECK(!loaded->editing.configured && loaded->client_width() == 840);
    CHECK(loaded->Text(Label::SelectAll) == "Select All");
    CHECK(&*loaded->presenter->window ==
          static_cast<apptraverse::ui::UiWindow*>(&*loaded));
    CHECK(loaded->text_controls.front()->owner.id() == RootId());
    CHECK(loaded->layout_state && loaded->device_state);
#ifdef _WIN32
    loaded->PrepareLanguage(ui::UiLanguage::kRussian);
    loaded->Configure(false, "Embedded", "");
    CHECK(loaded->presenter->title->Resolve(1) == "Чат SharedNode - Embedded");
    CHECK(loaded->Get(Control::Join).text == "Войти");
    chat::ChatView remote;
    remote.status_code = chat::ChatStatus::InvalidHostId;
    remote.participants.push_back({"Zoë", "", chat::Presence::Unknown});
    loaded->AcceptTransport(remote);
    CHECK(loaded->Get(Control::Status).text ==
          "Не в сети | Некорректный Aether ID хоста");
    CHECK(loaded->participants->rows.front()->primary_text->Resolve(1) ==
          "Zoë");
    CHECK(loaded->text_controls.at(6)->accessible_label == "Сообщение");
#else
    loaded->Configure(true, "Embedded", "");
    CHECK(loaded->Title() == "SharedNode Chat - Embedded");
#endif
  }
  std::cout << "Chat UI model PASS: send ACK revisions, RAM serialization, "
               "local constraints, typed controls/mirror\n";
}
