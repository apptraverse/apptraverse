#if !defined(_WIN32)
int main() { return 0; }
#else

#include <cstdlib>
#include <array>
#include <string>
#include <deque>
#include <iostream>
#include <memory>
#include <filesystem>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/registry.h"

#include "apptraverse/application_ui_session.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/platform/windows/win_ui_frame.h"
#include "apptraverse/platform/windows/win_ui_environment.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "window_catalog_graph.h"
#include "window_catalog_presenters.h"
#include "apptraverse/ui/ui_layout_state.h"
#include "apptraverse/ui/ui_push_button_presenter.h"
#include "window_catalog_ids.h"
#include "window_catalog_prepare.h"
#include "window_catalog_main_native_frame_queue.h"
#include "window_catalog_main_native_frame_wiring.h"
#include "apptraverse/ui/ui_presented_window.h"

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

// Fixture teardown and binding assertions are test-only; production only wires
// presenters to the session queue before native activation.
void UnwireWindowCatalogMainNativeFrameQueue(ae::Obj& gui_root) {
  ae::Domain* const domain = gui_root.domain;
  if (domain == nullptr) {
    return;
  }
  std::vector<ae::Obj*> objects;
  CollectLiveReachableObjects(gui_root, objects);
  auto& registry = ae::Registry::GetRegistry();
  for (ae::Obj* obj : objects) {
    if (registry.GenerationDistance(
            example::surfaces::CatalogDesktopWindowPresenter::kClassId,
            obj->GetClassId()) < 0) {
      continue;
    }
    static_cast<example::surfaces::CatalogDesktopWindowPresenter*>(obj)
        ->DetachSessionMainNativeFrameQueue();
  }
}

bool WindowCatalogMainNativeFrameQueuePrepareBinding(
    ae::Obj& gui_root, ae::ObjId main_presenter_id,
    WindowCatalogMainNativeFrameQueue& queue, bool require_presentation_pending) {
  ae::Domain* const domain = gui_root.domain;
  if (domain == nullptr || !main_presenter_id.is_valid()) {
    return false;
  }
  auto held = domain->Find(main_presenter_id);
  if (!held) {
    return false;
  }
  ae::Obj* const domain_presenter = &*held;
  auto& catalog = static_cast<WindowCatalog&>(gui_root);
  ae::ObjId const catalog_obj_id = gui_root.obj_id;
  ae::ObjId const main_window_id = catalog.persisted.main_window.id();
  std::vector<ae::Obj*> objects;
  CollectLiveReachableObjects(gui_root, objects);
  auto& registry = ae::Registry::GetRegistry();
  ae::Obj* reachable_presenter = nullptr;
  for (ae::Obj* obj : objects) {
    if (registry.GenerationDistance(
            example::surfaces::CatalogDesktopWindowPresenter::kClassId,
            obj->GetClassId()) < 0) {
      continue;
    }
    auto* presenter =
        static_cast<example::surfaces::CatalogDesktopWindowPresenter*>(obj);
    if (presenter->window.id() != main_window_id) {
      continue;
    }
    reachable_presenter = obj;
    break;
  }
  if (reachable_presenter == nullptr || reachable_presenter != domain_presenter) {
    return false;
  }
  auto& catalog_presenter =
      static_cast<example::surfaces::CatalogDesktopWindowPresenter&>(*held);
  if (catalog_presenter.SessionMainNativeFrameQueue() != &queue) {
    return false;
  }
  if (require_presentation_pending && catalog_presenter.presentation_loaded) {
    return false;
  }
  return true;
}

bool WindowCatalogMainNativeFrameQueueAttached(ae::Obj& gui_root) {
  auto& catalog = static_cast<WindowCatalog&>(gui_root);
  ae::ObjId const catalog_obj_id = gui_root.obj_id;
  ae::ObjId const main_window_id = catalog.persisted.main_window.id();
  std::vector<ae::Obj*> objects;
  CollectLiveReachableObjects(gui_root, objects);
  auto& registry = ae::Registry::GetRegistry();
  for (ae::Obj* obj : objects) {
    if (registry.GenerationDistance(
            example::surfaces::CatalogDesktopWindowPresenter::kClassId,
            obj->GetClassId()) < 0) {
      continue;
    }
    auto* presenter =
        static_cast<example::surfaces::CatalogDesktopWindowPresenter*>(obj);
    if (presenter->window.id() != main_window_id) {
      continue;
    }
    return presenter->SessionMainNativeFrameQueue() != nullptr;
  }
  return false;
}

constexpr std::uint32_t kListControlId = 5001;
constexpr std::uint32_t kAddControlId = 5002;
constexpr std::uint32_t kOpenControlId = 5003;
constexpr std::uint32_t kDeleteControlId = 5004;
constexpr std::uint32_t kLanguageControlId = 5005;

struct ClientRect {
  int x{0};
  int y{0};
  int width{0};
  int height{0};
};

ClientRect ReadChildInParentClient(HWND child) {
  ClientRect out{};
  RECT rect{};
  CHECK(GetWindowRect(child, &rect) != 0);
  HWND const parent = GetParent(child);
  CHECK(parent != nullptr);
  POINT top_left{rect.left, rect.top};
  POINT bottom_right{rect.right, rect.bottom};
  CHECK(MapWindowPoints(HWND_DESKTOP, parent, &top_left, 1) != 0);
  CHECK(MapWindowPoints(HWND_DESKTOP, parent, &bottom_right, 1) != 0);
  out.x = top_left.x;
  out.y = top_left.y;
  out.width = bottom_right.x - top_left.x;
  out.height = bottom_right.y - top_left.y;
  return out;
}

bool RectsOverlap(ClientRect const& a, ClientRect const& b) {
  return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
         b.y < a.y + a.height;
}

void SetMainClientSize(HWND hwnd, std::int32_t client_w, std::int32_t client_h,
                       bool interactive) {
  if (interactive) {
    SendMessageW(hwnd, WM_ENTERSIZEMOVE, 0, 0);
  }
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t cw = 0;
  std::int32_t ch = 0;
  ui::windows::ReadModelFrameFromHwnd(hwnd, x, y, cw, ch);
  int outer_w = 0;
  int outer_h = 0;
  ui::windows::ClientSizeToOuter(client_w, client_h, outer_w, outer_h);
  CHECK(SetWindowPos(hwnd, nullptr, x, y, outer_w, outer_h,
                     SWP_NOZORDER | SWP_NOACTIVATE) != 0);
  if (!interactive) {
    return;
  }
}

void EndInteractive(HWND hwnd) { SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0); }

void DispatchPendingWin32Messages() {
  MSG msg{};
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

struct Harness {
  ae::RamDomainStorage model_storage;
  ae::Domain model_domain{model_storage};
  WindowCatalog::ptr model_catalog;
  std::unique_ptr<ApplicationUiSession> ui_session;
  WindowCatalog* ui_catalog{nullptr};
  std::deque<ModelObjectProxy::ModelWork> model_work;
  ModelObjectProxy model_proxy;
  ui::windows::WinDesktopWindowPresenter* main_presenter{nullptr};
  ae::ObjId const list_presenter_id;
  ae::ObjId const main_window_id;

  WindowCatalogMainNativeFrameQueue main_native_frame_queue;

  Harness()
      : model_proxy{[this](ModelObjectProxy::ModelWork work) {
          model_work.push_back(std::move(work));
        }},
        list_presenter_id{window_catalog_demo::ToObjId(
            window_catalog_demo::ObjId::CatalogListPresenter)},
        main_window_id{window_catalog_demo::ToObjId(
            window_catalog_demo::ObjId::MainWindow)} {
    main_native_frame_queue.BindPostWork(
        [this](ModelObjectProxy::ModelWork work) {
          model_work.push_back(std::move(work));
        });
    model_catalog = BuildWindowCatalogGraph(model_domain);
    PrepareWindowCatalogForRun(*model_catalog);
    ByteSink initial;
    SerializeInitialPublication(*model_catalog, initial);
    ui_session = std::make_unique<ApplicationUiSession>(
        initial.bytes, nullptr, model_proxy, PresenterWireHook());
    ui_catalog = static_cast<WindowCatalog*>(&ui_session->root());
    ae::ObjId const main_presenter_id = window_catalog_demo::ToObjId(
        window_catalog_demo::ObjId::MainWindowPresenter);
    ae::Obj* const presenter_obj =
        &*ui_catalog->domain->Find(main_presenter_id);
    main_presenter =
        static_cast<ui::windows::WinDesktopWindowPresenter*>(presenter_obj);
    CHECK(main_presenter != nullptr);
    CHECK(main_presenter->hwnd() != nullptr);
    CHECK(WindowCatalogMainNativeFrameQueueAttached(*ui_catalog));
    DispatchPendingWin32Messages();
  }

  ~Harness() {
    if (ui_catalog != nullptr) {
      UnwireWindowCatalogMainNativeFrameQueue(*ui_catalog);
    }
    ui_session.reset();
    ui_catalog = nullptr;
    main_presenter = nullptr;
    DispatchPendingWin32Messages();
    main_native_frame_queue.ClosePosting();
  }

  UiPresentersPrepareHook PresenterWireHook() {
    return [this](ae::Obj& root) {
      WireWindowCatalogMainNativeFrameQueue(root, main_native_frame_queue);
    };
  }

  void PumpModelAndApply() {
    while (!model_work.empty()) {
      auto work = std::move(model_work.front());
      model_work.pop_front();
      work(model_domain);
    }
    ByteSink sink;
    SerializeStructuralNodePublication(*model_catalog, sink);
    ui_session->ApplyStructural(sink.bytes);
    DispatchPendingWin32Messages();
  }

  HWND MainHwnd() const { return main_presenter->hwnd(); }

  HWND ControlHwnd(std::uint32_t control_id) const {
    return GetDlgItem(MainHwnd(), static_cast<int>(control_id));
  }

  void ResizeClient(std::int32_t w, std::int32_t h, bool interactive) {
    SetMainClientSize(MainHwnd(), w, h, interactive);
    DispatchPendingWin32Messages();
    PumpModelAndApply();
  }
};

void ExpectReadableControl(HWND control, ClientRect const& rect) {
  HFONT const font = reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0));
  CHECK(font != nullptr);
  HDC const dc = GetDC(control);
  CHECK(dc != nullptr);
  HGDIOBJ const previous = SelectObject(dc, font);
  TEXTMETRICW metrics{};
  CHECK(GetTextMetricsW(dc, &metrics) != 0);
  CHECK(rect.height >= metrics.tmHeight);
  int const length = GetWindowTextLengthW(control);
  std::wstring caption(static_cast<std::size_t>(length) + 1, L'\0');
  GetWindowTextW(control, caption.data(), length + 1);
  caption.resize(static_cast<std::size_t>(length));
  RECT measured{0, 0, rect.width, 0};
  DrawTextW(dc, caption.c_str(), length, &measured,
            DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
  CHECK(measured.bottom - measured.top <= rect.height);
  SelectObject(dc, previous);
  ReleaseDC(control, dc);
}

void ExpectChromeIntegrity(HWND main) {
  RECT client{};
  CHECK(GetClientRect(main, &client) != 0);
  std::array<std::uint32_t, 5> const ids{kListControlId, kAddControlId,
      kOpenControlId, kDeleteControlId, kLanguageControlId};
  std::array<ClientRect, 5> rects{};
  for (std::size_t i = 0; i < ids.size(); ++i) {
    HWND const control = GetDlgItem(main, static_cast<int>(ids[i]));
    CHECK(control != nullptr);
    rects[i] = ReadChildInParentClient(control);
    auto const& rect = rects[i];
    CHECK(rect.width > 0 && rect.height > 0);
    CHECK(rect.x >= 0 && rect.y >= 0);
    SCROLLINFO horizontal{sizeof(SCROLLINFO), SIF_RANGE | SIF_POS};
    SCROLLINFO vertical{sizeof(SCROLLINFO), SIF_RANGE | SIF_POS};
    GetScrollInfo(main, SB_HORZ, &horizontal);
    GetScrollInfo(main, SB_VERT, &vertical);
    // Large system text can legitimately exceed the viewport. It must remain
    // reachable through the actual native scrollbar rather than be clipped.
    CHECK(rect.x + rect.width <= client.right ||
          rect.x + rect.width + horizontal.nPos <= horizontal.nMax + 1);
    CHECK(rect.y + rect.height <= client.bottom ||
          rect.y + rect.height + vertical.nPos <= vertical.nMax + 1);
    if (ids[i] != kListControlId) ExpectReadableControl(control, rect);
    for (std::size_t j = 0; j < i; ++j) CHECK(!RectsOverlap(rects[j], rect));
  }
}

void ExpectWideChrome(HWND main, std::int32_t, std::int32_t) {
  ExpectChromeIntegrity(main);
  auto const list = ReadChildInParentClient(GetDlgItem(main, kListControlId));
  auto const add = ReadChildInParentClient(GetDlgItem(main, kAddControlId));
  CHECK(list.x + list.width <= add.x);
}

void ExpectTallChrome(HWND main, std::int32_t, std::int32_t) {
  ExpectChromeIntegrity(main);
  auto const list = ReadChildInParentClient(GetDlgItem(main, kListControlId));
  auto const add = ReadChildInParentClient(GetDlgItem(main, kAddControlId));
  CHECK(list.y + list.height <= add.y);
}

void TestPrepareBindsQueueBeforeNativeOnLoad() {
  ae::RamDomainStorage model_storage;
  ae::Domain model_domain{model_storage};
  WindowCatalogMainNativeFrameQueue queue;
  std::deque<ModelObjectProxy::ModelWork> model_work;
  ModelObjectProxy model_proxy{[&](ModelObjectProxy::ModelWork work) {
    model_work.push_back(std::move(work));
  }};
  queue.BindPostWork([&](ModelObjectProxy::ModelWork work) {
    model_work.push_back(std::move(work));
  });
  WindowCatalog::ptr model_catalog = BuildWindowCatalogGraph(model_domain);
  PrepareWindowCatalogForRun(*model_catalog);
  ByteSink initial;
  SerializeInitialPublication(*model_catalog, initial);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  ByteSource in;
  in.data = initial.bytes.data();
  in.size = initial.bytes.size();
  auto root = LoadInitialPublication(in, ui_domain, ui_storage);
  auto& ui_catalog = static_cast<WindowCatalog&>(*root);
  ae::ObjId const main_presenter_id = window_catalog_demo::ToObjId(
      window_catalog_demo::ObjId::MainWindowPresenter);

  WireWindowCatalogMainNativeFrameQueue(ui_catalog, queue);
  ae::Obj* const domain_presenter =
      &*ui_catalog.domain->Find(main_presenter_id);
  auto* const win_view =
      static_cast<ui::windows::WinDesktopWindowPresenter*>(domain_presenter);
  CHECK(win_view != nullptr);
  CHECK(static_cast<void*>(domain_presenter) == static_cast<void*>(win_view));
  CHECK(WindowCatalogMainNativeFrameQueuePrepareBinding(
      ui_catalog, main_presenter_id, queue, true));
  InitializePresenters(ui_catalog, nullptr, &model_proxy);
  CHECK(WindowCatalogMainNativeFrameQueuePrepareBinding(
      ui_catalog, main_presenter_id, queue, false));
  CHECK(WindowCatalogMainNativeFrameQueueAttached(ui_catalog));
  UnloadPresenters(ui_catalog);
  DispatchPendingWin32Messages();
  queue.ClosePosting();
}

void TestBurstResizeWithoutPerStepPump() {
  Harness h;
  HWND const main = h.MainHwnd();
  for (int i = 0; i < 31; ++i) {
    std::int32_t const w = (i % 2 == 0) ? 900 : 500;
    std::int32_t const ch = (i % 2 == 0) ? 500 : 900;
    SetMainClientSize(main, w, ch, true);
    DispatchPendingWin32Messages();
  }
  SetMainClientSize(main, 900, 500, false);
  DispatchPendingWin32Messages();
  EndInteractive(main);
  DispatchPendingWin32Messages();
  h.PumpModelAndApply();
  ExpectWideChrome(main, 900, 500);
}

void TestNativeAdaptiveChromeResizePath() {
  Harness h;
  HWND const main = h.MainHwnd();
  h.model_catalog->AddWindow();
  h.PumpModelAndApply();
  HWND const list0 = h.ControlHwnd(kListControlId);
  CHECK(list0 != nullptr);
  CHECK(SendMessageW(list0, LB_SETCURSEL, 0, 0) != LB_ERR);
  ae::ObjId const list_id = h.list_presenter_id;

  auto const initial_viewport = h.model_catalog->persisted.main_client_viewport;
  h.ResizeClient(900, 500, false);
  ExpectWideChrome(main, 900, 500);
  CHECK(h.model_catalog->persisted.main_client_viewport.width == initial_viewport.width);
  CHECK(h.model_catalog->persisted.main_client_viewport.height == initial_viewport.height);

  SetMainClientSize(main, 500, 900, true);
  DispatchPendingWin32Messages();
  h.PumpModelAndApply();
  ExpectTallChrome(main, 500, 900);
  EndInteractive(main);
  DispatchPendingWin32Messages();
  h.PumpModelAndApply();

  h.ResizeClient(600, 600, false);
  ExpectWideChrome(main, 600, 600);

  h.ResizeClient(900, 500, false);
  ExpectWideChrome(main, 900, 500);

  HWND const list1 = h.ControlHwnd(kListControlId);
  CHECK(list1 == list0);
  CHECK(GetDlgItem(main, static_cast<int>(kAddControlId)) != nullptr);
  CHECK(SendMessageW(list1, LB_GETCURSEL, 0, 0) == 0);
  CHECK(h.ui_catalog->persisted.presenters.list_presenter.id() == list_id);
  CHECK(h.model_catalog->persisted.main_window.id() == h.main_window_id);
}

void TestLocalResizeDoesNotDependOnModelPump() {
  Harness h;
  h.PumpModelAndApply();
  auto const viewport = h.model_catalog->persisted.main_client_viewport;
  auto const layout_generation = h.model_catalog->persisted.main_window->layout->Generation();
  auto const& add_presenter = static_cast<ui::UiPushButtonPresenter const&>(
      *h.model_catalog->persisted.presenters.add_button_presenter);
  std::array<std::int32_t, 4> const original{add_presenter.layout_x,
      add_presenter.layout_y, add_presenter.layout_width, add_presenter.layout_height};
  SetMainClientSize(h.MainHwnd(), 500, 900, true);
  DispatchPendingWin32Messages();
  // WM_SIZE already placed the controls while the model queue is still pending.
  ExpectTallChrome(h.MainHwnd(), 500, 900);
  CHECK(h.model_catalog->persisted.main_client_viewport.width == viewport.width);
  CHECK(h.model_catalog->persisted.main_client_viewport.height == viewport.height);
  CHECK(h.model_catalog->persisted.main_window->layout->Generation() == layout_generation);
  CHECK(add_presenter.layout_x == original[0]);
  CHECK(add_presenter.layout_y == original[1]);
  CHECK(add_presenter.layout_width == original[2]);
  CHECK(add_presenter.layout_height == original[3]);
  EndInteractive(h.MainHwnd());
  h.PumpModelAndApply();
  ExpectTallChrome(h.MainHwnd(), 500, 900);
  CHECK(h.model_catalog->persisted.main_client_viewport.width == viewport.width);
  CHECK(h.model_catalog->persisted.main_client_viewport.height == viewport.height);
}

void TestRuntimeConstraintOverrideAndClear() {
  Harness h;
  h.ResizeClient(900, 500, false);
  ExpectWideChrome(h.MainHwnd(), 900, 500);
  auto model_layout = h.model_catalog->persisted.main_window->layout;
  auto ui_layout = h.ui_catalog->persisted.main_window->layout;
  HWND const original_list = h.ControlHwnd(kListControlId);
  HWND const original_add = h.ControlHwnd(kAddControlId);
  auto constraints = ui_layout->EffectiveRoot();
  constraints.flow = ui::UiLayoutFlow::kVertical;
  h.model_proxy.Invoke(ui_layout.id(), &ui::UiLayoutState::SetOverride, constraints);
  CHECK(!model_layout->has_override);
  CHECK(!ui_layout->has_override);
  ExpectWideChrome(h.MainHwnd(), 900, 500);
  h.PumpModelAndApply();
  CHECK(model_layout->has_override);
  CHECK(ui_layout->has_override);
  ExpectTallChrome(h.MainHwnd(), 900, 500);
  CHECK(h.ControlHwnd(kListControlId) == original_list);
  CHECK(h.ControlHwnd(kAddControlId) == original_add);
  h.model_proxy.Invoke(ui_layout.id(), &ui::UiLayoutState::ClearOverride);
  CHECK(model_layout->has_override);
  h.PumpModelAndApply();
  CHECK(!model_layout->has_override);
  CHECK(!ui_layout->has_override);
  ExpectWideChrome(h.MainHwnd(), 900, 500);
  CHECK(h.ControlHwnd(kListControlId) == original_list);
  CHECK(h.ControlHwnd(kAddControlId) == original_add);
}

void TestSystemSettingsNotificationsStayLocal() {
  Harness h;
  h.ResizeClient(900, 500, false);
  h.PumpModelAndApply();
  auto const model_generation = h.model_catalog->Generation();
  auto const layout_generation = h.model_catalog->persisted.main_window->layout->Generation();
  auto const pending_count = h.model_work.size();
  HWND const original_button = h.ControlHwnd(kAddControlId);
  SendMessageW(h.MainHwnd(), WM_SETTINGCHANGE, SPI_SETNONCLIENTMETRICS, 0);
  SendMessageW(h.MainHwnd(), WM_THEMECHANGED, 0, 0);
  SendMessageW(h.MainHwnd(), WM_SYSCOLORCHANGE, 0, 0);
  DispatchPendingWin32Messages();
  CHECK(h.model_catalog->Generation() == model_generation);
  CHECK(h.model_catalog->persisted.main_window->layout->Generation() == layout_generation);
  CHECK(h.model_work.size() == pending_count);
  CHECK(h.main_presenter->LayoutScale() > 0);
  CHECK(h.main_presenter->TextScale() > 0);
  CHECK(h.ControlHwnd(kAddControlId) == original_button);
  ExpectWideChrome(h.MainHwnd(), 900, 500);
}

void TestLocalAppearancePreferences() {
  using namespace ui::windows;
  auto original = GetWinAppearancePreferences();
  Harness first;
  Harness second;
  first.PumpModelAndApply();
  second.PumpModelAndApply();
  auto first_generation = first.model_catalog->Generation();
  auto second_generation = second.model_catalog->Generation();
  SetWinAppearancePreferences({WinThemeMode::kDark, false});
  DispatchPendingWin32Messages();
  if (!first.main_presenter->HighContrast()) {
    CHECK(first.main_presenter->DarkTheme());
    CHECK(second.main_presenter->DarkTheme());
    SetWinAppearancePreferences({WinThemeMode::kDark, true});
    DispatchPendingWin32Messages();
    CHECK(first.main_presenter->BackgroundColor() == RGB(32,32,32));
    CHECK(second.main_presenter->BackgroundColor() == RGB(32,32,32));
    SetWinAppearancePreferences({WinThemeMode::kLight, true});
    DispatchPendingWin32Messages();
    CHECK(!first.main_presenter->DarkTheme());
    CHECK(!second.main_presenter->DarkTheme());
    CHECK(first.main_presenter->BackgroundColor() == GetSysColor(COLOR_WINDOW));
  } else {
    CHECK(first.main_presenter->BackgroundColor() == GetSysColor(COLOR_WINDOW));
  }
  CHECK(first.model_catalog->Generation() == first_generation);
  CHECK(second.model_catalog->Generation() == second_generation);
  auto const ini = std::filesystem::temp_directory_path() /
      ("apptraverse-appearance-" + std::to_string(ae::ObjId::GenerateUnique().id()) + ".ini");
  example::surfaces::SaveCatalogAppearance({WinThemeMode::kDark, true}, ini.wstring());
  SetWinAppearancePreferences({WinThemeMode::kAuto, false});
  example::surfaces::LoadCatalogAppearance(ini.wstring());
  CHECK(GetWinAppearancePreferences().theme == WinThemeMode::kDark);
  CHECK(GetWinAppearancePreferences().soften_dark);
  CHECK(first.model_catalog->Generation() == first_generation);
  CHECK(second.model_catalog->Generation() == second_generation);
  std::filesystem::remove(ini);
  SetWinAppearancePreferences(original);
  DispatchPendingWin32Messages();
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureWindowCatalogGraphRegistration();
  apptraverse::test::TestPrepareBindsQueueBeforeNativeOnLoad();
  apptraverse::test::TestBurstResizeWithoutPerStepPump();
  apptraverse::test::TestNativeAdaptiveChromeResizePath();
  apptraverse::test::TestLocalResizeDoesNotDependOnModelPump();
  apptraverse::test::TestRuntimeConstraintOverrideAndClear();
  apptraverse::test::TestSystemSettingsNotificationsStayLocal();
  apptraverse::test::TestLocalAppearancePreferences();
  std::cout << "window_catalog_win32_chrome_adaptive_test OK\n";
  return 0;
}

#endif
