#include <cstdlib>
#include <deque>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/node.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_presented_window.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_string.h"
#include "apptraverse/ui/ui_window.h"

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

ae::ObjId const kRootId{0xC0010000u};
ae::ObjId const kWindowId{0xC0010001u};
ae::ObjId const kPresenterId{0xC0010002u};
ae::ObjId const kLocaleId{0xC0010003u};
ae::ObjId const kTitleId{0xC0010004u};

char const* const kEnTitle = "Contract EN title";
char const* const kRuTitle = "Contract RU title";

ae::ObjFlags ClearUnloaded(ae::ObjFlags flags) {
  return static_cast<ae::ObjFlags>(flags & ~ae::ObjFlags::kUnloaded);
}

ae::ObjFlags OpenObjFlags(ae::ObjFlags flags) {
  return ClearUnloaded(static_cast<ae::ObjFlags>(
      flags & ~ae::ObjFlags::kUnloadedByDefault));
}

class ContractSetMarkerEvent;
class ContractClosePresentedWindowEvent;
class ContractOpenPresentedWindowEvent;
class HeadlessDesktopWindowPresenter;

class ContractWindow : public NodeFor<ContractWindow, ui::UiDesktopWindow> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::ContractWindow", ContractWindow,
                           ui::UiDesktopWindow, 0)

 protected:
  ContractWindow() = default;

 public:
  explicit ContractWindow(ae::ObjProp prop) : NodeFor{prop} {}
};

class HeadlessDesktopWindowPresenter
    : public ui::UiDesktopWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::HeadlessDesktopWindowPresenter",
                           HeadlessDesktopWindowPresenter,
                           ui::UiDesktopWindowPresenter, 0)

 protected:
  HeadlessDesktopWindowPresenter() = default;

 public:
  explicit HeadlessDesktopWindowPresenter(ae::ObjProp prop)
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
  int apply_requested_frame_count{0};
  std::int32_t simulated_native_x{0};
  std::int32_t simulated_native_y{0};
  std::int32_t simulated_native_cw{800};
  std::int32_t simulated_native_ch{600};
  std::string last_resolved_localized_text;
  void* owner_harness{nullptr};

  void OnLoad() override;
  void OnUnload() override;
  void OnModelChanged() override;
  void ApplyRequestedFrameFromMirror() override;
  void RefreshResolvedTitle();
};

class ContractApplicationRoot : public NodeFor<ContractApplicationRoot> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::ContractApplicationRoot",
                           ContractApplicationRoot, Node, 0)

 protected:
  ContractApplicationRoot() = default;

 public:
  explicit ContractApplicationRoot(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(presented_window), AE_MMBR(locale),
                    AE_MMBR(localized_title), AE_MMBR(marker),
                    AE_MMBR(presented_open))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, presented_window, locale, localized_title, marker,
        presented_open);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, presented_window, locale, localized_title, marker,
        presented_open);
  }

  ContractWindow::ptr presented_window;
  ui::UiLocaleSettings::ptr locale;
  ui::UiLocalizationString::ptr localized_title;
  int marker{0};
  bool presented_open{false};

  void Apply(ContractSetMarkerEvent const& event);
  void Apply(ContractClosePresentedWindowEvent const& event);
  void Apply(ContractOpenPresentedWindowEvent const& event);

  void ClosePresentedWindowImpl();
  void OpenPresentedWindowImpl();
};

class ContractSetMarkerEvent
    : public EventFor<ContractApplicationRoot, ContractSetMarkerEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::ContractSetMarkerEvent",
                           ContractSetMarkerEvent, Event, 0)

 protected:
  ContractSetMarkerEvent() = default;

 public:
  explicit ContractSetMarkerEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(marker))

  int marker{0};
};

class ContractClosePresentedWindowEvent
    : public EventFor<ContractApplicationRoot,
                      ContractClosePresentedWindowEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::ContractClosePresentedWindowEvent",
                           ContractClosePresentedWindowEvent, Event, 0)

 protected:
  ContractClosePresentedWindowEvent() = default;

 public:
  explicit ContractClosePresentedWindowEvent(ae::ObjProp prop)
      : EventFor{prop} {}
};

class ContractOpenPresentedWindowEvent
    : public EventFor<ContractApplicationRoot, ContractOpenPresentedWindowEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::ContractOpenPresentedWindowEvent",
                           ContractOpenPresentedWindowEvent, Event, 0)

 protected:
  ContractOpenPresentedWindowEvent() = default;

 public:
  explicit ContractOpenPresentedWindowEvent(ae::ObjProp prop)
      : EventFor{prop} {}
};

void HeadlessDesktopWindowPresenter::OnModelChanged() {
  ++on_model_changed_count;
  RefreshResolvedTitle();
}

void HeadlessDesktopWindowPresenter::ApplyRequestedFrameFromMirror() {
  ++apply_requested_frame_count;
  ui::UiDesktopWindow::ptr const desktop = desktop_window();
  if (!desktop.is_valid()) {
    return;
  }
  simulated_native_x = desktop->x();
  simulated_native_y = desktop->y();
  simulated_native_cw = desktop->client_width();
  simulated_native_ch = desktop->client_height();
}

void HeadlessDesktopWindowPresenter::RefreshResolvedTitle() {
  if (!title.is_valid() || !locale.is_valid()) {
    last_resolved_localized_text.clear();
    return;
  }
  last_resolved_localized_text = title->Resolve(locale->language_index());
}

HeadlessDesktopWindowPresenter& HeadlessOn(ui::UiDesktopWindow& window) {
  CHECK(window.presenter.is_valid());
  CHECK(window.presenter.is_loaded());
  auto& registry = ae::Registry::GetRegistry();
  CHECK(registry.GenerationDistance(HeadlessDesktopWindowPresenter::kClassId,
                                    window.presenter->GetClassId()) >= 0);
  return static_cast<HeadlessDesktopWindowPresenter&>(*window.presenter);
}

void ContractApplicationRoot::ClosePresentedWindowImpl() {
  if (!presented_open) {
    return;
  }
  if (presented_window.is_valid() && presented_window.is_loaded()) {
    if (presented_window->presenter.is_valid() &&
        presented_window->presenter.is_loaded()) {
      presented_window->presenter.Save();
    }
    ContractWindow::ptr::MakeFromThis(&*presented_window).Save();
  }
  presented_open = false;
  if (presented_window.is_valid() && presented_window.is_loaded()) {
    if (presented_window->presenter.is_valid() &&
        presented_window->presenter.is_loaded()) {
      presented_window->presenter.Reset();
    }
    ContractWindow::ptr::MakeFromThis(&*presented_window).Save();
    presented_window.Reset();
  }
  ContractApplicationRoot::ptr::MakeFromThis(this).Save();
  NoteMaterializedChange();
}

void ContractApplicationRoot::OpenPresentedWindowImpl() {
  if (presented_open) {
    return;
  }
  ae::Domain& domain = *this->domain;
  ae::ObjId const window_id = presented_window.id();
  if (!presented_window.is_loaded()) {
    ae::DomainGraph load_graph{&domain,
                               ae::GraphSerializationScope::LocalPersistent};
    if (ae::Ptr<ae::Obj> loaded = load_graph.LoadRoot(window_id)) {
      presented_window = ContractWindow::ptr::MakeFromThis(
          static_cast<ContractWindow*>(loaded.get()));
    }
  }
  if (!presented_window.is_loaded() || presented_window.id() != window_id) {
    return;
  }
  if (presented_window->presenter.is_valid() &&
      !presented_window->presenter.is_loaded()) {
    presented_window->presenter.Load();
  }
  presented_window.SetFlags(OpenObjFlags(presented_window.flags()));
  if (presented_window->presenter.is_valid()) {
    presented_window->presenter.SetFlags(
        OpenObjFlags(presented_window->presenter.flags()));
  }
  presented_open = true;
  ContractApplicationRoot::ptr::MakeFromThis(this).Save();
  NoteMaterializedChange();
}

void ContractApplicationRoot::Apply(ContractSetMarkerEvent const& event) {
  marker = event.marker;
}

void ContractApplicationRoot::Apply(ContractClosePresentedWindowEvent const&) {
  ClosePresentedWindowImpl();
}

void ContractApplicationRoot::Apply(ContractOpenPresentedWindowEvent const&) {
  OpenPresentedWindowImpl();
}

APPTRAVERSE_REGISTER(ContractApplicationRoot);
APPTRAVERSE_REGISTER(ContractSetMarkerEvent);
APPTRAVERSE_REGISTER(ContractClosePresentedWindowEvent);
APPTRAVERSE_REGISTER(ContractOpenPresentedWindowEvent);
APPTRAVERSE_REGISTER(ContractWindow);
APPTRAVERSE_REGISTER(HeadlessDesktopWindowPresenter);

void EnsureContractTestRegistration() {
  (void)&g_apptraverse_registrar_ContractApplicationRoot;
  (void)&g_apptraverse_registrar_ContractSetMarkerEvent;
  (void)&g_apptraverse_registrar_ContractClosePresentedWindowEvent;
  (void)&g_apptraverse_registrar_ContractOpenPresentedWindowEvent;
  (void)&g_apptraverse_registrar_ContractWindow;
  (void)&g_apptraverse_registrar_HeadlessDesktopWindowPresenter;
}

std::size_t CountJournalEventsWithClass(Node const& node,
                                        std::uint32_t class_id) {
  std::size_t count = 0;
  for (auto const& entry : node.journal) {
    if (entry.event && entry.event->GetClassId() == class_id) {
      ++count;
    }
  }
  return count;
}

ui::UiLocalizationString::ptr BuildLazyTitle(ae::Domain& domain,
                                             ae::ObjId const& title_id,
                                             ae::ObjId& en_id, ae::ObjId& ru_id) {
  auto en = ui::UiString::ptr::Create(ae::CreateWith{domain});
  en->utf8 = kEnTitle;
  auto ru = ui::UiString::ptr::Create(ae::CreateWith{domain});
  ru->utf8 = kRuTitle;
  en_id = en->obj_id;
  ru_id = ru->obj_id;
  en.Save();
  ru.Save();

  auto localized = ui::UiLocalizationString::ptr::Create(
      ae::CreateWith{domain}.with_id(title_id));
  InitializeRuntimeNode(*localized);
  localized->strings = {
      ui::UiString::ptr::Declare(
          ae::CreateWith{domain}
              .with_id(en_id)
              .with_flags(ae::ObjFlags::kUnloadedByDefault)),
      ui::UiString::ptr::Declare(
          ae::CreateWith{domain}
              .with_id(ru_id)
              .with_flags(ae::ObjFlags::kUnloadedByDefault)),
  };
  ui::UiLocalizationString::ptr::MakeFromThis(&*localized).Save();
  return localized;
}

struct ContractHarness;

struct ContractHarness {
  int presenter_unload_events{0};
  int presenter_load_events{0};
  ae::RamDomainStorage model_storage;
  ae::RamDomainStorage ui_storage;
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};
  ContractApplicationRoot::ptr model_root;
  ContractApplicationRoot::ptr ui_root;
  std::deque<ModelObjectProxy::ModelWork> deferred_model_work;
  ModelObjectProxy model_proxy;
  ae::ObjId en_string_id{};
  ae::ObjId ru_string_id{};

  explicit ContractHarness(bool materialize_en_title)
      : model_proxy{[this](ModelObjectProxy::ModelWork work) {
          deferred_model_work.push_back(std::move(work));
        }} {
    EnsureObjectRegistration();
    ui::EnsureUiPresenterRegistration();
    EnsureContractTestRegistration();

    model_root = ContractApplicationRoot::ptr::Create(
        ae::CreateWith{model_domain}.with_id(kRootId));
    InitializeRuntimeNode(*model_root);
    model_root->locale = ui::UiLocaleSettings::ptr::Create(
        ae::CreateWith{model_domain}.with_id(kLocaleId));
    InitializeRuntimeNode(*model_root->locale, *model_root);
    model_root->localized_title =
        BuildLazyTitle(model_domain, kTitleId, en_string_id, ru_string_id);
    model_root->presented_window = ContractWindow::ptr::Create(
        ae::CreateWith{model_domain}.with_id(kWindowId));
    InitializeRuntimeNode(*model_root->presented_window, *model_root);
    auto headless_presenter = HeadlessDesktopWindowPresenter::ptr::Create(
        ae::CreateWith{model_domain}.with_id(kPresenterId));
    model_root->presented_window->presenter = headless_presenter;
    headless_presenter->window = model_root->presented_window;
    headless_presenter->title = model_root->localized_title;
    headless_presenter->locale = model_root->locale;
    model_root->presented_open = true;

    if (materialize_en_title) {
      model_root->localized_title->MaterializeLanguage(0);
    }

    PublishInitialToUi();
  }

  ~ContractHarness() {
    if (ui_root) {
      UnloadPresenters(*ui_root);
    }
  }

  HeadlessDesktopWindowPresenter& headless() {
    return HeadlessOn(*ui_root->presented_window);
  }

  void WireHeadlessPresenterOwner() {
    if (auto held = ui_domain.Find(kPresenterId)) {
      static_cast<HeadlessDesktopWindowPresenter&>(*held).owner_harness =
          this;
    }
  }

  void PumpModel() {
    while (!deferred_model_work.empty()) {
      auto work = std::move(deferred_model_work.front());
      deferred_model_work.pop_front();
      work(model_domain);
    }
  }

  void PublishInitialToUi() {
    ByteSink sink;
    SerializeInitialPublication(*model_root, sink);
    ByteSource in;
    in.data = sink.bytes.data();
    in.size = sink.bytes.size();
    auto ui_obj = LoadInitialPublication(in, ui_domain, ui_storage);
    ui_root = ContractApplicationRoot::ptr::MakeFromThis(
        static_cast<ContractApplicationRoot*>(ui_obj.get()));
    ui_root->presented_window->presenter->model_proxy = &model_proxy;
    headless().owner_harness = this;
    InitializePresenters(*ui_root, nullptr, &model_proxy);
  }

  void PublishStructuralFromRoot() {
    ByteSink sink;
    SerializeStructuralNodePublication(*model_root, sink);
    ByteSource in;
    in.data = sink.bytes.data();
    in.size = sink.bytes.size();
    WireHeadlessPresenterOwner();
    ApplyStructuralPublicationAndUpdatePresenters(
        in, ui_domain, ui_storage, *ui_root, nullptr, &model_proxy);
    WireHeadlessPresenterOwner();
  }

  void PublishStructuralFromWindow() {
    ByteSink sink;
    SerializeStructuralNodePublication(*model_root->presented_window, sink);
    ByteSource in;
    in.data = sink.bytes.data();
    in.size = sink.bytes.size();
    WireHeadlessPresenterOwner();
    ApplyStructuralPublicationAndUpdatePresenters(
        in, ui_domain, ui_storage, *ui_root, nullptr, &model_proxy);
    WireHeadlessPresenterOwner();
  }
};

void HeadlessDesktopWindowPresenter::OnLoad() {
  ++on_load_count;
  if (owner_harness != nullptr) {
    auto* harness = static_cast<ContractHarness*>(owner_harness);
    ++harness->presenter_load_events;
  }
}

void HeadlessDesktopWindowPresenter::OnUnload() {
  ++on_unload_count;
  if (owner_harness != nullptr) {
    auto* harness = static_cast<ContractHarness*>(owner_harness);
    ++harness->presenter_unload_events;
  }
}

void TestUiPresentedWindowHierarchy() {
  CHECK((std::is_base_of_v<ui::UiPresentedWindow, ui::UiDesktopWindow>));
  CHECK((std::is_base_of_v<ui::UiWindow, ui::UiPresentedWindow>));
  CHECK((std::is_base_of_v<ui::UiDesktopWindow, ContractWindow>));
  CHECK(ui::UiPresentedWindow::kClassId != ui::UiDesktopWindow::kClassId);
  CHECK(ui::UiPresentedWindow::kClassId != ui::UiWindow::kClassId);
}

void TestPresenterReachableOnlyViaWindow() {
  ContractHarness h{false};
  CHECK(h.model_root->presented_window->presenter.is_valid());
  CHECK(h.model_root->presented_window->presenter.id() == kPresenterId);
  CHECK(h.ui_root->presented_window->presenter.id() == kPresenterId);
  CHECK(&*h.model_root->presented_window->presenter !=
        &*h.ui_root->presented_window->presenter);
  CHECK(h.model_root->presented_window->presenter->domain !=
        h.ui_root->presented_window->presenter->domain);
}

void TestWindowPresenterSaveLoadRoundtrip() {
  ContractHarness h{true};
  h.model_root->presented_window->RequestFrame(77, 88, 640, 480);
  ContractWindow::ptr::MakeFromThis(&*h.model_root->presented_window).Save();
  h.model_root->presented_window->presenter.Save();

  h.model_root->presented_window.Reset();
  CHECK(!h.model_domain.Find(kWindowId));

  auto reloaded = ContractWindow::ptr::Declare(
      ae::CreateWith{h.model_domain}.with_id(kWindowId));
  reloaded.Load();
  CHECK(reloaded->presenter.is_valid());
  reloaded->presenter.Load();
  CHECK(reloaded->presenter.id() == kPresenterId);
  CHECK(reloaded->presenter->window.is_valid());
  CHECK(reloaded->presenter->window.is_loaded());
  CHECK(reloaded->presenter->window.id() == kWindowId);
  CHECK(&*reloaded->presenter->window == &*reloaded);
  CHECK(reloaded->presenter->title.id() == kTitleId);
  CHECK(reloaded->presenter->locale.id() == kLocaleId);
  CHECK(reloaded->presenter->title->domain == reloaded->domain);
  CHECK(reloaded->presenter->locale->domain == reloaded->domain);
  CHECK(reloaded->x() == 77);
  CHECK(reloaded->y() == 88);
}

void TestEventModelPublicationUiMirror() {
  ContractHarness h{false};
  CHECK(h.model_root->presented_window->presenter.id() == kPresenterId);
  CHECK(h.model_root->presented_window.id() == h.ui_root->presented_window.id());
  CHECK(&*h.model_root->presented_window != &*h.ui_root->presented_window);
  CHECK(h.model_root->presented_window->domain != h.ui_root->presented_window->domain);
  CHECK(h.headless().desktop_window()->domain == &h.ui_domain);

  int const marker_before = h.ui_root->marker;
  auto event = ContractSetMarkerEvent::ptr::Create(
      ae::CreateWith{h.model_domain});
  event->marker = 42;
  h.model_root->Commit(std::move(event));
  CHECK(h.ui_root->marker == marker_before);
  CHECK(CountJournalEventsWithClass(*h.model_root,
                                    ContractSetMarkerEvent::kClassId) >= 1);

  h.PublishStructuralFromRoot();
  CHECK(h.ui_root->marker == 42);
  CHECK(h.model_root->marker == 42);
}

void TestNativeFrameReport() {
  ContractHarness h{false};
  h.headless().model_proxy = &h.model_proxy;
  h.headless().SubmitFrameFromNative(11, 22, 640, 480);
  h.PumpModel();
  CHECK(h.model_root->presented_window->x() == 11);
  CHECK(h.model_root->presented_window->y() == 22);
  CHECK(CountJournalEventsWithClass(
            *h.model_root->presented_window,
            ui::ReportUiDesktopWindowFrameFromNativeEvent::kClassId) == 1);

  h.PublishStructuralFromWindow();
  CHECK(h.ui_root->presented_window->x() == 11);
  CHECK(h.ui_root->presented_window->y() == 22);
  CHECK(h.headless().apply_requested_frame_count == 0);

  std::size_t const journal_after_first =
      h.model_root->presented_window->journal.size();
  h.headless().SubmitFrameFromNative(11, 22, 640, 480);
  h.PumpModel();
  CHECK(h.model_root->presented_window->journal.size() == journal_after_first);
  CHECK(CountJournalEventsWithClass(
            *h.model_root->presented_window,
            ui::ReportUiDesktopWindowFrameFromNativeEvent::kClassId) == 1);
}

void TestModelRequestedFrame() {
  ContractHarness h{false};
  h.model_root->presented_window->RequestFrame(50, 60, 700, 500);
  CHECK(CountJournalEventsWithClass(
            *h.model_root->presented_window,
            ui::RequestUiDesktopWindowFrameEvent::kClassId) == 1);
  CHECK(h.headless().simulated_native_x != 50);

  h.PublishStructuralFromWindow();
  CHECK(h.headless().apply_requested_frame_count == 1);
  CHECK(h.headless().simulated_native_x == 50);
  CHECK(h.headless().simulated_native_y == 60);
  CHECK(h.ui_root->presented_window->x() == 50);

  int const apply_count = h.headless().apply_requested_frame_count;
  h.model_root->presented_window->RequestFrame(50, 60, 700, 500);
  CHECK(CountJournalEventsWithClass(
            *h.model_root->presented_window,
            ui::RequestUiDesktopWindowFrameEvent::kClassId) == 1);
  h.PublishStructuralFromWindow();
  CHECK(h.headless().apply_requested_frame_count == apply_count);
}

void TestLazyLocalization() {
  ContractHarness h{true};
  CHECK(h.model_root->localized_title->strings[0].is_loaded());
  CHECK(!h.model_root->localized_title->strings[1].is_loaded());
  CHECK(h.headless().last_resolved_localized_text == kEnTitle);

  auto lang = ui::SetUiLanguageIndexEvent::ptr::Create(
      ae::CreateWith{h.model_domain});
  lang->language_index = 1;
  h.model_root->locale->Commit(std::move(lang));
  h.model_root->localized_title->MaterializeLanguage(1);
  CHECK(!h.model_root->localized_title->strings[0].is_loaded());
  CHECK(h.model_root->localized_title->strings[1].is_loaded());
  CHECK(h.model_root->localized_title->strings[0].id() == h.en_string_id);
  CHECK(h.model_root->localized_title->strings[1].id() == h.ru_string_id);
  CHECK(h.headless().last_resolved_localized_text == kEnTitle);

  h.PublishStructuralFromRoot();
  CHECK(h.headless().last_resolved_localized_text == kRuTitle);
}

void TestLazyPresentedWindowUnloadReload() {
  ContractHarness h{true};
  h.headless().SubmitFrameFromNative(100, 120, 640, 480);
  h.PumpModel();
  h.PublishStructuralFromWindow();
  std::string const title_before = h.headless().last_resolved_localized_text;
  CHECK(h.headless().on_load_count == 1);
  int const load_events_before_close = h.presenter_load_events;
  CHECK(load_events_before_close == 1);

  auto close = ContractClosePresentedWindowEvent::ptr::Create(
      ae::CreateWith{h.model_domain});
  h.model_root->Commit(std::move(close));
  CHECK(!h.model_root->presented_open);
  CHECK(h.model_root->presented_window.is_valid());
  CHECK(!h.model_root->presented_window.is_loaded());
  CHECK(!h.model_domain.Find(kWindowId));

  h.PublishStructuralFromRoot();
  CHECK(h.presenter_unload_events == 1);
  CHECK(!h.ui_root->presented_window.is_loaded());
  CHECK(!h.ui_domain.Find(kWindowId));

  auto open = ContractOpenPresentedWindowEvent::ptr::Create(
      ae::CreateWith{h.model_domain});
  h.model_root->Commit(std::move(open));
  CHECK(h.model_root->presented_open);
  CHECK(h.model_root->presented_window.is_loaded());
  CHECK(h.model_root->presented_window.id() == kWindowId);
  CHECK(h.model_root->presented_window->presenter.id() == kPresenterId);
  CHECK(h.model_root->presented_window->presenter->window.id() == kWindowId);
  CHECK(h.model_root->presented_window->presenter->title.id() == kTitleId);
  CHECK(h.model_root->presented_window->presenter->locale.id() == kLocaleId);

  int presenter_loads_before_reopen = 0;
  if (ae::Ptr<ae::Obj> held = h.ui_domain.Find(kPresenterId)) {
    presenter_loads_before_reopen =
        static_cast<HeadlessDesktopWindowPresenter&>(*held).on_load_count;
  }
  h.PublishStructuralFromRoot();
  CHECK(h.ui_root->presented_window->presenter.is_loaded());
  CHECK(h.ui_domain.Find(kWindowId));
  CHECK(h.ui_root->presented_window.id() == kWindowId);
  CHECK(h.ui_root->presented_window->presenter.id() == kPresenterId);
  CHECK(h.ui_root->presented_window->presenter->window.id() == kWindowId);
  CHECK(h.ui_root->presented_window->presenter->title.id() == kTitleId);
  CHECK(h.ui_root->presented_window->presenter->locale.id() == kLocaleId);
  ae::Ptr<ae::Obj> reloaded_presenter = h.ui_domain.Find(kPresenterId);
  CHECK(reloaded_presenter);
  CHECK(static_cast<HeadlessDesktopWindowPresenter&>(*reloaded_presenter)
            .on_load_count == presenter_loads_before_reopen + 1);
  CHECK(h.ui_root->presented_window->x() == 100);
  CHECK(h.ui_root->presented_window->y() == 120);
  CHECK(h.headless().last_resolved_localized_text == title_before);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::test::TestUiPresentedWindowHierarchy();
  apptraverse::test::TestPresenterReachableOnlyViaWindow();
  apptraverse::test::TestWindowPresenterSaveLoadRoundtrip();
  apptraverse::test::TestEventModelPublicationUiMirror();
  apptraverse::test::TestNativeFrameReport();
  apptraverse::test::TestModelRequestedFrame();
  apptraverse::test::TestLazyLocalization();
  apptraverse::test::TestLazyPresentedWindowUnloadReload();
  std::cout << "apptraverse_application_contract_headless_test OK\n";
  return 0;
}
