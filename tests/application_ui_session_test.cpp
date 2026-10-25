#include <cstdlib>
#include <deque>
#include <iostream>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/application_ui_session.h"
#include "apptraverse/event_for.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/node_for.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_presented_window.h"
#include "apptraverse/ui/ui_presented_window_composition.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_string.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (false)

ae::ObjId const kRootId{0xB6B60001u};
ae::ObjId const kWindowId{0xB6B60002u};
ae::ObjId const kPresenterId{0xB6B60003u};
ae::ObjId const kLocaleId{0xB6B60004u};
ae::ObjId const kTitleId{0xB6B60005u};

int g_session_presenter_load_total{0};
int g_session_presenter_unload_total{0};

class UiSessionSetMarkerEvent;
class UiSessionCloseWindowEvent;
class UiSessionOpenWindowEvent;

class SessionHeadlessPresenter : public ui::UiDesktopWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::SessionHeadlessPresenter",
                           SessionHeadlessPresenter,
                           ui::UiDesktopWindowPresenter, 0)

 protected:
  SessionHeadlessPresenter() = default;

 public:
  explicit SessionHeadlessPresenter(ae::ObjProp prop)
      : ui::UiDesktopWindowPresenter{prop} {}

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_);
  }

  int on_load_count{0};
  int on_unload_count{0};
  int on_model_changed_count{0};

  void OnLoad() override {
    ++on_load_count;
    ++g_session_presenter_load_total;
  }

  void OnUnload() override {
    ++on_unload_count;
    ++g_session_presenter_unload_total;
  }

  void OnModelChanged() override { ++on_model_changed_count; }
};

class SessionWindow : public NodeFor<SessionWindow, ui::UiDesktopWindow> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::SessionWindow", SessionWindow,
                           ui::UiDesktopWindow, 0)

 protected:
  SessionWindow() = default;

 public:
  explicit SessionWindow(ae::ObjProp prop) : NodeFor{prop} {}
};

class UiSessionTestRoot : public NodeFor<UiSessionTestRoot> {
  APPTRAVERSE_OBJECT(UiSessionTestRoot, Node, 0)

 protected:
  UiSessionTestRoot() = default;

 public:
  explicit UiSessionTestRoot(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(presented_window), AE_MMBR(marker),
                    AE_MMBR(presented_open))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, presented_window, marker, presented_open);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, presented_window, marker, presented_open);
  }

  SessionWindow::ptr presented_window;
  int marker{0};
  bool presented_open{false};

  void Apply(UiSessionSetMarkerEvent const& event);
  void Apply(UiSessionCloseWindowEvent const& event);
  void Apply(UiSessionOpenWindowEvent const& event);

  void ClosePresentedWindowImpl();
  void OpenPresentedWindowImpl();
};

class UiSessionSetMarkerEvent
    : public EventFor<UiSessionTestRoot, UiSessionSetMarkerEvent> {
  APPTRAVERSE_OBJECT(UiSessionSetMarkerEvent, Event, 0)

 protected:
  UiSessionSetMarkerEvent() = default;

 public:
  explicit UiSessionSetMarkerEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(marker))

  int marker{0};
};

class UiSessionCloseWindowEvent
    : public EventFor<UiSessionTestRoot, UiSessionCloseWindowEvent> {
  APPTRAVERSE_OBJECT(UiSessionCloseWindowEvent, Event, 0)

 protected:
  UiSessionCloseWindowEvent() = default;

 public:
  explicit UiSessionCloseWindowEvent(ae::ObjProp prop) : EventFor{prop} {}
};

class UiSessionOpenWindowEvent
    : public EventFor<UiSessionTestRoot, UiSessionOpenWindowEvent> {
  APPTRAVERSE_OBJECT(UiSessionOpenWindowEvent, Event, 0)

 protected:
  UiSessionOpenWindowEvent() = default;

 public:
  explicit UiSessionOpenWindowEvent(ae::ObjProp prop) : EventFor{prop} {}
};

APPTRAVERSE_REGISTER(UiSessionTestRoot);
APPTRAVERSE_REGISTER(UiSessionSetMarkerEvent);
APPTRAVERSE_REGISTER(UiSessionCloseWindowEvent);
APPTRAVERSE_REGISTER(UiSessionOpenWindowEvent);
APPTRAVERSE_REGISTER(SessionWindow);
APPTRAVERSE_REGISTER(SessionHeadlessPresenter);

void EnsureUiSessionTestRegistration() {
  (void)&g_apptraverse_registrar_UiSessionTestRoot;
  (void)&g_apptraverse_registrar_UiSessionSetMarkerEvent;
  (void)&g_apptraverse_registrar_UiSessionCloseWindowEvent;
  (void)&g_apptraverse_registrar_UiSessionOpenWindowEvent;
  (void)&g_apptraverse_registrar_SessionWindow;
  (void)&g_apptraverse_registrar_SessionHeadlessPresenter;
}

void UiSessionTestRoot::ClosePresentedWindowImpl() {
  if (!presented_open) {
    return;
  }
  presented_open = false;
  ui::UnloadPresentedWindow(presented_window);
  UiSessionTestRoot::ptr::MakeFromThis(this).Save();
  NoteMaterializedChange();
}

void UiSessionTestRoot::OpenPresentedWindowImpl() {
  if (presented_open) {
    return;
  }
  ui::LoadPresentedWindow(presented_window);
  presented_open = true;
  UiSessionTestRoot::ptr::MakeFromThis(this).Save();
  NoteMaterializedChange();
}

void UiSessionTestRoot::Apply(UiSessionSetMarkerEvent const& event) {
  marker = event.marker;
  NoteMaterializedChange();
}

void UiSessionTestRoot::Apply(UiSessionCloseWindowEvent const&) {
  ClosePresentedWindowImpl();
}

void UiSessionTestRoot::Apply(UiSessionOpenWindowEvent const&) {
  OpenPresentedWindowImpl();
}

SessionHeadlessPresenter& HeadlessOn(SessionWindow& window) {
  return static_cast<SessionHeadlessPresenter&>(
      *window.presenter.operator->());
}

struct ModelFixture {
  ae::RamDomainStorage model_storage;
  ae::Domain model_domain{model_storage};
  UiSessionTestRoot::ptr model_root;
  std::deque<ModelObjectProxy::ModelWork> deferred_model_work;
  ModelObjectProxy model_proxy;

  ModelFixture()
      : model_proxy{[this](ModelObjectProxy::ModelWork work) {
          deferred_model_work.push_back(std::move(work));
        }} {
    EnsureObjectRegistration();
    ui::EnsureUiPresenterRegistration();
    EnsureUiSessionTestRegistration();

    model_root = UiSessionTestRoot::ptr::Create(
        ae::CreateWith{model_domain}.with_id(kRootId));
    InitializeRuntimeNode(*model_root);

    auto locale = ui::UiLocaleSettings::ptr::Create(
        ae::CreateWith{model_domain}.with_id(kLocaleId));
    InitializeRuntimeNode(*locale, *model_root);

    auto en = ui::UiString::ptr::Create(ae::CreateWith{model_domain});
    en->utf8 = "EN";
    auto localized = ui::UiLocalizationString::ptr::Create(
        ae::CreateWith{model_domain}.with_id(kTitleId));
    localized->strings = {ui::UiString::ptr::MakeFromThis(&*en)};
    InitializeRuntimeNode(*localized, *model_root);

    model_root->presented_window = SessionWindow::ptr::Create(
        ae::CreateWith{model_domain}.with_id(kWindowId));
    auto presenter = SessionHeadlessPresenter::ptr::Create(
        ae::CreateWith{model_domain}.with_id(kPresenterId));
    ui::ComposePresentedWindow(model_root->presented_window, presenter,
                               localized, locale);
    InitializeRuntimeNode(*model_root->presented_window, *model_root);
    model_root->presented_open = true;
  }

  std::vector<std::uint8_t> SerializeInitial() const {
    ByteSink sink;
    SerializeInitialPublication(*model_root, sink);
    return sink.bytes;
  }

  std::vector<std::uint8_t> SerializeStructural() {
    ByteSink sink;
    SerializeStructuralNodePublication(*model_root, sink);
    return sink.bytes;
  }

  void PumpModel() {
    while (!deferred_model_work.empty()) {
      auto work = std::move(deferred_model_work.front());
      deferred_model_work.pop_front();
      work(model_domain);
    }
  }
};

void TestInitialMirror() {
  ModelFixture fixture;
  auto const initial_bytes = fixture.SerializeInitial();

  ApplicationUiSession ui_session(initial_bytes, nullptr, fixture.model_proxy);
  auto& ui_root = static_cast<UiSessionTestRoot&>(ui_session.root());
  auto& ui_window = static_cast<SessionWindow&>(*ui_root.presented_window);
  SessionHeadlessPresenter& presenter = HeadlessOn(ui_window);

  CHECK(ui_root.obj_id == kRootId);
  CHECK(ui_root.domain != fixture.model_root->domain);
  CHECK(&ui_window != &*fixture.model_root->presented_window);
  CHECK(presenter.on_load_count == 1);
  CHECK(presenter.on_model_changed_count >= 1);
}

void TestStructuralUpdate() {
  ModelFixture fixture;
  ApplicationUiSession ui_session(fixture.SerializeInitial(), nullptr,
                                  fixture.model_proxy);
  auto& ui_root = static_cast<UiSessionTestRoot&>(ui_session.root());
  SessionHeadlessPresenter& presenter =
      HeadlessOn(static_cast<SessionWindow&>(*ui_root.presented_window));
  int const model_changed_before = presenter.on_model_changed_count;

  auto event = UiSessionSetMarkerEvent::ptr::Create(
      ae::CreateWith{fixture.model_domain});
  event->marker = 99;
  fixture.model_root->Commit(std::move(event));

  CHECK(ui_root.marker == 0);

  ui_session.ApplyStructural(fixture.SerializeStructural());
  CHECK(ui_root.marker == 99);
  CHECK(fixture.model_root->marker == 99);
  CHECK(presenter.on_model_changed_count > model_changed_before);
  CHECK(presenter.on_load_count == 1);
}

void TestPresenterLifecycleCloseOpen() {
  ModelFixture fixture;
  g_session_presenter_load_total = 0;
  g_session_presenter_unload_total = 0;
  ApplicationUiSession ui_session(fixture.SerializeInitial(), nullptr,
                                  fixture.model_proxy);
  auto& ui_root = static_cast<UiSessionTestRoot&>(ui_session.root());
  SessionHeadlessPresenter& presenter =
      HeadlessOn(static_cast<SessionWindow&>(*ui_root.presented_window));
  CHECK(g_session_presenter_load_total == 1);

  auto close_ev = UiSessionCloseWindowEvent::ptr::Create(
      ae::CreateWith{fixture.model_domain});
  fixture.model_root->Commit(std::move(close_ev));
  ui_session.ApplyStructural(fixture.SerializeStructural());
  CHECK(presenter.on_unload_count == 1);
  CHECK(g_session_presenter_unload_total == 1);
  CHECK(!ui_root.presented_window.is_loaded());

  auto open_ev = UiSessionOpenWindowEvent::ptr::Create(
      ae::CreateWith{fixture.model_domain});
  fixture.model_root->Commit(std::move(open_ev));
  ui_session.ApplyStructural(fixture.SerializeStructural());
  SessionHeadlessPresenter& reopened =
      HeadlessOn(static_cast<SessionWindow&>(*ui_root.presented_window));
  CHECK(reopened.on_load_count == 1);
  CHECK(g_session_presenter_load_total == 2);
  CHECK(ui_root.presented_window.is_loaded());
}

void TestDestructorUnloadsPresenters() {
  ModelFixture fixture;
  g_session_presenter_unload_total = 0;
  {
    ApplicationUiSession ui_session(fixture.SerializeInitial(), nullptr,
                                    fixture.model_proxy);
    SessionHeadlessPresenter& presenter = HeadlessOn(
        static_cast<SessionWindow&>(
            *static_cast<UiSessionTestRoot&>(ui_session.root())
                 .presented_window));
    CHECK(presenter.on_load_count == 1);
    CHECK(presenter.on_unload_count == 0);
  }
  CHECK(g_session_presenter_unload_total == 1);
}

void TestModelProxyTargetsModelDomain() {
  ModelFixture fixture;
  ApplicationUiSession ui_session(fixture.SerializeInitial(), nullptr,
                                  fixture.model_proxy);
  auto& ui_root = static_cast<UiSessionTestRoot&>(ui_session.root());

  fixture.model_proxy.EnqueueModelWork([&](ae::Domain& domain) {
    auto event = UiSessionSetMarkerEvent::ptr::Create(
        ae::CreateWith{domain});
    event->marker = 7;
    fixture.model_root->Commit(std::move(event));
  });
  fixture.PumpModel();
  CHECK(fixture.model_root->marker == 7);
  CHECK(ui_root.marker == 0);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::test::TestInitialMirror();
  apptraverse::test::TestStructuralUpdate();
  apptraverse::test::TestPresenterLifecycleCloseOpen();
  apptraverse::test::TestDestructorUnloadsPresenters();
  apptraverse::test::TestModelProxyTargetsModelDomain();
  std::cout << "application_ui_session_test OK\n";
  return 0;
}
