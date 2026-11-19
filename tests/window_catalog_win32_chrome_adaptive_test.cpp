#if !defined(_WIN32)
int main() { return 0; }
#else

#include <cstdlib>
#include <array>
#include <algorithm>
#include <string>
#include <deque>
#include <iostream>
#include <memory>
#include <optional>

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
#include <commctrl.h>
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
  // MapWindowPoints returns the translation delta, so zero can mean success
  // for a borderless parent at the desktop origin. Use the BOOL conversion API.
  CHECK(ScreenToClient(parent, &top_left));
  CHECK(ScreenToClient(parent, &bottom_right));
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
  ui::windows::ClientSizeToOuter(client_w, client_h, outer_w, outer_h, GetMenu(hwnd) != nullptr);
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

  explicit Harness(std::optional<ui::UiDeviceEnvironment> previous = {},
                   std::optional<ui::UiWindowEnvironment> placement = {})
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
    ui::windows::EnsureWinProcessDpiAwareness();
    auto local = model_catalog->persisted.device_state;
    if (previous) local->ObserveEnvironment(*previous, ui::UiEnvironmentReason::kStartup);
    if (placement) local->ObserveWindow(*placement, ui::UiEnvironmentReason::kStartup);
    ui::windows::WinUiEnvironment reader{nullptr};
    local->ObserveEnvironment(reader.snapshot(), ui::UiEnvironmentReason::kStartup);
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
  std::array<std::uint32_t, 4> const ids{kListControlId, kAddControlId,
      kOpenControlId, kDeleteControlId};
  std::array<ClientRect, 4> rects{};
  CHECK(GetDlgItem(main, kLanguageControlId) == nullptr);
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
  auto const layout_generation = h.model_catalog->persisted.main_window->layout_state->Generation();
  auto const window_generation = h.model_catalog->persisted.main_window->Generation();
  auto const restore_width = h.model_catalog->persisted.main_window->restore_client_width();
  auto const restore_height = h.model_catalog->persisted.main_window->restore_client_height();
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
  CHECK(h.model_catalog->persisted.main_window->layout_state->Generation() == layout_generation);
  CHECK(add_presenter.layout_x == original[0]);
  CHECK(add_presenter.layout_y == original[1]);
  CHECK(add_presenter.layout_width == original[2]);
  CHECK(add_presenter.layout_height == original[3]);
  EndInteractive(h.MainHwnd());
  h.PumpModelAndApply();
  ExpectTallChrome(h.MainHwnd(), 500, 900);
  CHECK(h.model_catalog->persisted.main_client_viewport.width == viewport.width);
  CHECK(h.model_catalog->persisted.main_client_viewport.height == viewport.height);
  CHECK(h.model_catalog->persisted.main_window->Generation() == window_generation);
  CHECK(h.model_catalog->persisted.main_window->restore_client_width() == restore_width);
  CHECK(h.model_catalog->persisted.main_window->restore_client_height() == restore_height);
  auto const* placement = h.model_catalog->persisted.device_state->PlacementFor(h.main_window_id);
  CHECK(placement != nullptr);
  CHECK(placement->frame_dip.width != restore_width || placement->frame_dip.height != restore_height);
}

void TestRuntimeConstraintOverrideAndClear() {
  Harness h;
  h.ResizeClient(900, 500, false);
  ExpectWideChrome(h.MainHwnd(), 900, 500);
  auto model_layout = h.model_catalog->persisted.main_window->layout_state;
  auto ui_layout = h.ui_catalog->persisted.main_window->layout_state;
  CHECK(&h.model_catalog->persisted.main_window->layout ==
        &h.model_catalog->persisted.main_window->layout_state);
  CHECK(&h.ui_catalog->persisted.main_window->layout ==
        &h.ui_catalog->persisted.main_window->layout_state);
  CHECK(model_layout.id() == ui_layout.id());
  CHECK(model_layout.Load().get() != ui_layout.Load().get());
  HWND const original_list = h.ControlHwnd(kListControlId);
  HWND const original_add = h.ControlHwnd(kAddControlId);
  auto const& menu = static_cast<CatalogUiState const&>(*h.ui_catalog->persisted.device_state).menu;
  CHECK(menu->layout_items.size() == 4);
  CHECK(menu->layout_items[2].label->Resolve(0) == "Layout: Vertical");
  h.model_proxy.Invoke(h.ui_catalog->persisted.device_state.id(), &CatalogUiState::ExecuteLayoutCommand, 2u);
  CHECK(!model_layout->has_override);
  CHECK(!ui_layout->has_override);
  ExpectWideChrome(h.MainHwnd(), 900, 500);
  h.PumpModelAndApply();
  CHECK(model_layout->has_override);
  CHECK(ui_layout->has_override);
  ExpectTallChrome(h.MainHwnd(), 900, 500);
  CHECK(h.ControlHwnd(kListControlId) == original_list);
  CHECK(h.ControlHwnd(kAddControlId) == original_add);
  h.model_proxy.Invoke(h.ui_catalog->persisted.device_state.id(), &CatalogUiState::ExecuteLayoutCommand, 3u);
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
  auto const layout_generation = h.model_catalog->persisted.main_window->layout_state->Generation();
  auto const window_generation = h.model_catalog->persisted.main_window->Generation();
  auto const restore_width = h.model_catalog->persisted.main_window->restore_client_width();
  auto const restore_height = h.model_catalog->persisted.main_window->restore_client_height();
  auto const pending_count = h.model_work.size();
  HWND const original_button = h.ControlHwnd(kAddControlId);
  SendMessageW(h.MainHwnd(), WM_SETTINGCHANGE, SPI_SETNONCLIENTMETRICS, 0);
  SendMessageW(h.MainHwnd(), WM_THEMECHANGED, 0, 0);
  SendMessageW(h.MainHwnd(), WM_SYSCOLORCHANGE, 0, 0);
  DispatchPendingWin32Messages();
  CHECK(h.model_catalog->Generation() == model_generation);
  CHECK(h.model_catalog->persisted.main_window->layout_state->Generation() == layout_generation);
  CHECK(h.model_work.size() == pending_count);
  CHECK(h.main_presenter->LayoutScale() > 0);
  CHECK(h.main_presenter->TextScale() > 0);
  CHECK(h.ControlHwnd(kAddControlId) == original_button);
  ExpectWideChrome(h.MainHwnd(), 900, 500);
}

struct NativeProjectionProbe {
  HWND hwnd;
  bool track_geometry;
  bool track_nonclient;
  unsigned text_writes{}, font_writes{}, frame_writes{}, list_writes{}, nonclient_paints{};
  explicit NativeProjectionProbe(HWND target, bool geometry = true, bool nonclient = false)
      : hwnd{target}, track_geometry{geometry}, track_nonclient{nonclient} {
    CHECK(SetWindowSubclass(hwnd, Observe, 0xAC71, reinterpret_cast<DWORD_PTR>(this)));
  }
  ~NativeProjectionProbe() { CHECK(RemoveWindowSubclass(hwnd, Observe, 0xAC71)); }
  static LRESULT CALLBACK Observe(HWND hwnd, UINT msg, WPARAM w, LPARAM l,
                                   UINT_PTR, DWORD_PTR context) {
    auto& probe = *reinterpret_cast<NativeProjectionProbe*>(context);
    if (msg == WM_SETTEXT) ++probe.text_writes;
    if (msg == WM_SETFONT) ++probe.font_writes;
    if (msg == WM_WINDOWPOSCHANGING && probe.track_geometry) ++probe.frame_writes;
    if (msg == WM_NCPAINT && probe.track_nonclient) ++probe.nonclient_paints;
    if (msg == LB_RESETCONTENT || msg == LB_DELETESTRING || msg == LB_INSERTSTRING ||
        msg == LB_ADDSTRING) ++probe.list_writes;
    return DefSubclassProc(hwnd, msg, w, l);
  }
  void ExpectNoWrites() const {
    CHECK(text_writes == 0);
    CHECK(font_writes == 0);
    CHECK(frame_writes == 0);
    CHECK(list_writes == 0);
    CHECK(nonclient_paints == 0);
  }
};

void TestFiniteLayoutMaximumDoesNotRepeatedlyRedrawScrollbars() {
  Harness h;
  // Acknowledge initial native placement before intentionally enlarging the
  // viewport beyond the root maximum; do not race the startup resolution.
  h.PumpModelAndApply();
  h.PumpModelAndApply();
  double const scale = h.main_presenter->LayoutScale();
  h.ResizeClient(static_cast<int>(900 * scale), static_cast<int>(600 * scale), false);
  auto layout = h.ui_catalog->persisted.main_window->layout_state;
  auto changed = layout->EffectiveRoot();
  changed.max_width = 600;
  changed.max_height = 400;
  h.model_proxy.Invoke(layout.id(), &ui::UiLayoutState::SetOverride, changed);
  h.PumpModelAndApply();
  h.PumpModelAndApply();
  RECT client{}; CHECK(GetClientRect(h.MainHwnd(), &client));
  SCROLLINFO horizontal{sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS};
  CHECK(GetScrollInfo(h.MainHwnd(), SB_HORZ, &horizontal));
  CHECK(horizontal.nPage < static_cast<UINT>(client.right));
  CHECK(horizontal.nPage == static_cast<UINT>(horizontal.nMax - horizontal.nMin + 1));
  CHECK(RedrawWindow(h.MainHwnd(), nullptr, nullptr,
                     RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW | RDW_ALLCHILDREN));
  DispatchPendingWin32Messages();
  NativeProjectionProbe probe{h.MainHwnd(), true, true};
  for (int pass = 0; pass < 3; ++pass) {
    h.main_presenter->RefreshLayout();
    h.PumpModelAndApply();
  }
  probe.ExpectNoWrites();
}

void TestPositionOnlyPublicationPreservesNativeControls() {
  Harness h;
  // Populate a genuinely scrolling native list, retaining one visible child.
  for (int i = 0; i < 20; ++i) {
    h.model_catalog->AddWindow();
    if (i != 19) {
      auto& holder = static_cast<WindowHolder&>(*h.model_catalog->persisted.list->rows.back());
      h.model_catalog->CloseHolderWindow(holder);
    }
  }
  h.model_catalog->SubmitSelectionFromUi(10);
  h.PumpModelAndApply();
  h.ResizeClient(900, 250, false);
  HWND const list = h.ControlHwnd(kListControlId);
  SetActiveWindow(h.MainHwnd());
  SetFocus(list);
  h.PumpModelAndApply();
  h.PumpModelAndApply();
  CHECK(SendMessageW(list, LB_SETTOPINDEX, 5, 0) != LB_ERR);
  LRESULT const selection = SendMessageW(list, LB_GETCURSEL, 0, 0);
  LRESULT const top = SendMessageW(list, LB_GETTOPINDEX, 0, 0);
  CHECK(selection == 10 && top > 0);
  CHECK(GetFocus() == list);
  auto* holder = static_cast<WindowHolder*>(&*h.ui_catalog->persisted.list->rows.back());
  auto& child = static_cast<ui::windows::WinDesktopWindowPresenter&>(*holder->window->presenter);
  std::vector<std::unique_ptr<NativeProjectionProbe>> probes;
  probes.push_back(std::make_unique<NativeProjectionProbe>(h.MainHwnd()));
  for (auto id : {kListControlId, kAddControlId, kOpenControlId, kDeleteControlId})
    probes.push_back(std::make_unique<NativeProjectionProbe>(h.ControlHwnd(id)));
  // Moving the child itself is expected; its title/font must still stay quiet.
  probes.push_back(std::make_unique<NativeProjectionProbe>(child.hwnd(), false));
  auto const local_generation = h.model_catalog->persisted.device_state->Generation();
  RECT initial{}; CHECK(GetWindowRect(child.hwnd(), &initial));
  MONITORINFO monitor{sizeof(monitor)};
  CHECK(GetMonitorInfoW(MonitorFromWindow(child.hwnd(), MONITOR_DEFAULTTONEAREST), &monitor));
  int const direction = initial.right + 40 < monitor.rcWork.right ? 1 : -1;
  SendMessageW(child.hwnd(), WM_ENTERSIZEMOVE, 0, 0);
  for (int step = 1; step <= 8; ++step) {
    CHECK(SetWindowPos(child.hwnd(), nullptr, initial.left + direction * step * 4,
                       initial.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
    h.PumpModelAndApply();
  }
  EndInteractive(child.hwnd());
  h.PumpModelAndApply();
  h.PumpModelAndApply();
  CHECK(h.model_catalog->persisted.device_state->Generation() > local_generation);
  CHECK(h.ControlHwnd(kListControlId) == list);
  CHECK(SendMessageW(list, LB_GETCURSEL, 0, 0) == selection);
  CHECK(SendMessageW(list, LB_GETTOPINDEX, 0, 0) == top);
  CHECK(GetFocus() == list);
  for (auto const& probe : probes) probe->ExpectNoWrites();
}

void TestLocalAppearancePreferences() {
  using namespace ui;
  Harness first;
  Harness second;
  first.PumpModelAndApply();
  second.PumpModelAndApply();
  auto first_generation = first.model_catalog->Generation();
  auto second_generation = second.model_catalog->Generation();
  first.model_proxy.Invoke(first.model_catalog.id(), &WindowCatalog::AddWindow);
  first.PumpModelAndApply();
  first_generation = first.model_catalog->Generation();
  auto* holder = static_cast<WindowHolder*>(&*first.ui_catalog->persisted.list->rows.front());
  auto& child = static_cast<ui::windows::WinDesktopWindowPresenter&>(*holder->window->presenter);
  auto local = first.model_catalog->persisted.device_state;
  auto second_local = second.model_catalog->persisted.device_state;
  using CatalogPresenter = example::surfaces::CatalogDesktopWindowPresenter;
  HMENU const settings = GetSubMenu(GetMenu(first.MainHwnd()), 0);
  CHECK(settings != nullptr);
  CHECK(GetMenuItemCount(settings) == 4);
  auto command = [&](UINT id, UiAppearancePreferences preferences) {
    auto const previous = local->appearance;
    SendMessageW(first.MainHwnd(), WM_COMMAND, MAKEWPARAM(id, 0), 0);
    // No speculative mutation of either UI mirror or canonical graph.
    CHECK(local->appearance == previous);
    CHECK(first.ui_catalog->persisted.device_state->appearance == previous);
    first.PumpModelAndApply();
    CHECK(local->appearance == preferences);
    CHECK(first.ui_catalog->persisted.device_state->appearance == preferences);
    for (UINT choice = CatalogPresenter::kThemeFromSystem; choice <= CatalogPresenter::kThemeCustomDark; ++choice)
      CHECK(((GetMenuState(settings, choice, MF_BYCOMMAND) & MF_CHECKED) != 0) == (choice == id));
  };
  command(CatalogPresenter::kThemeDark, {UiThemeMode::kDark, false});
  if (!first.main_presenter->HighContrast()) {
    CHECK(first.main_presenter->DarkTheme());
    auto const stock_background = first.main_presenter->BackgroundColor();
    command(CatalogPresenter::kThemeCustomDark, {UiThemeMode::kDark, true});
    CHECK(first.main_presenter->BackgroundColor() == RGB(32,32,32));
    CHECK(first.main_presenter->BackgroundColor() != stock_background);
    CHECK(child.BackgroundColor() == RGB(32,32,32));
    command(CatalogPresenter::kThemeLight, {UiThemeMode::kLight, false});
    CHECK(!first.main_presenter->DarkTheme());
    CHECK(first.main_presenter->BackgroundColor() == GetSysColor(COLOR_WINDOW));
  }
  command(CatalogPresenter::kThemeFromSystem, {UiThemeMode::kAuto, false});
  CHECK(first.model_catalog->Generation() == first_generation);
  SendMessageW(first.MainHwnd(), WM_COMMAND, MAKEWPARAM(CatalogPresenter::kLanguageFirst + 1, 0), 0);
  first.PumpModelAndApply();
  CHECK((GetMenuState(settings, CatalogPresenter::kThemeFromSystem, MF_BYCOMMAND) & MF_CHECKED) != 0);
  wchar_t translated[64]{};
  CHECK(GetMenuStringW(GetMenu(first.MainHwnd()), 0, translated, 64, MF_BYPOSITION));
  CHECK(std::wstring{translated} == L"&Тема");
  CHECK(second_local->appearance == UiAppearancePreferences{});
  CHECK(first.model_catalog->persisted.locale->language_index() == 1);
  CHECK(static_cast<CatalogUiState const&>(*first.ui_catalog->persisted.device_state)
      .menu->layout_items[2].label->Resolve(1) == "Раскладка: вертикально");
  CHECK(first.model_catalog->persisted.locale->object_scope().root_id == local.id());
  CHECK(second.model_catalog->Generation() == second_generation);
}

void TestStartupObservationAndMonitorResolution() {
  using namespace ui;
  ui::windows::EnsureWinProcessDpiAwareness();
  ui::windows::WinUiEnvironment reader{nullptr};
  auto previous = reader.snapshot();
  CHECK(!previous.monitors.empty());
  previous.system_dpi_x += 48;
  previous.system_dpi_y += 48;
  previous.text_scale_milli += 100;
  previous.monitors.push_back({"disconnected-monitor", {10000, 0, 1920, 1080},
                              {10000, 0, 1920, 1040}, false, 0, 60});
  UiWindowEnvironment saved{ae::ObjId{window_catalog_demo::ToObjId(
      window_catalog_demo::ObjId::MainWindow)}, "disconnected-monitor", 144, 144,
      {50, 50, 600, 400}};
  Harness h(previous, saved);
  auto local = h.model_catalog->persisted.device_state;
  CHECK(local->last_reason == UiEnvironmentReason::kStartup);
  CHECK((local->last_changes & (kEnvironmentDpi | kEnvironmentDisplays | kEnvironmentText)) ==
        (kEnvironmentDpi | kEnvironmentDisplays | kEnvironmentText));
  auto const* placement = local->PlacementFor(saved.window_id);
  CHECK(placement && placement->resolution == UiMonitorResolution::kMissing);
  CHECK(placement->preferred_monitor == "disconnected-monitor");
  CHECK(placement->resolved_monitor != placement->preferred_monitor);
  auto const& resolution = *static_cast<UiWindowMonitorResolvedEvent const*>(
      &*local->journal.back().event);
  CHECK(resolution.reason == UiEnvironmentReason::kStartup);
  CHECK(resolution.resolution == UiMonitorResolution::kMissing);
  CHECK(ui::windows::GetWinWindowMonitorKey(h.MainHwnd()) == placement->resolved_monitor);
  RECT bounds{}; GetWindowRect(h.MainHwnd(), &bounds);
  MONITORINFO info{sizeof(info)};
  CHECK(GetMonitorInfoW(MonitorFromWindow(h.MainHwnd(), MONITOR_DEFAULTTONEAREST), &info));
  CHECK(bounds.left >= info.rcWork.left && bounds.top >= info.rcWork.top);
  CHECK(bounds.right <= info.rcWork.right && bounds.bottom <= info.rcWork.bottom);
  auto const resolved_monitor = placement->resolved_monitor;
  h.PumpModelAndApply();
  auto const observed = local->windows.front();
  CHECK(observed.dpi_x == ui::windows::GetWinWindowDpi(h.MainHwnd()));
  CHECK(observed.monitor_key == resolved_monitor);
  CHECK(local->PlacementFor(saved.window_id)->preferred_monitor == "disconnected-monitor");
  auto generation = local->Generation();
  auto pending = h.model_work.size();
  for (int i = 0; i < 3; ++i) {
    SendMessageW(h.MainHwnd(), WM_DISPLAYCHANGE, 32, MAKELPARAM(1, 1));
    SendMessageW(h.MainHwnd(), WM_SETTINGCHANGE, SPI_SETNONCLIENTMETRICS, 0);
  }
  DispatchPendingWin32Messages();
  CHECK(local->Generation() == generation);
  CHECK(h.model_work.size() == pending);
  CHECK(local->environment.monitors.size() == reader.snapshot().monitors.size());
}

void TestFirstPublicationCarriesResolutionAfterNativeObservation() {
  Harness h;
  // Initial UI creation has queued its first native observation, but no model
  // publication has acknowledged it. Coalesce a work-area resolution after it.
  h.model_work.push_back([id = h.main_window_id](ae::Domain& domain) {
    auto catalog = WindowCatalog::ptr::MakeFromThis(static_cast<WindowCatalog*>(&*domain.Find(
        ae::ObjId{window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)})));
    auto local = catalog->persisted.device_state;
    auto environment = local->environment;
    auto& work = environment.monitors.front().work_area;
    work.x += 80;
    work.width = std::max(200, work.width - 80);
    local->ObserveEnvironment(environment, ui::UiEnvironmentReason::kSystemNotification);
    CHECK(local->PlacementFor(id)->revision > 1);
  });
  h.PumpModelAndApply();
  auto const& local = *h.model_catalog->persisted.device_state;
  auto const* placement = local.PlacementFor(h.main_window_id);
  auto const monitor = std::find_if(local.environment.monitors.begin(), local.environment.monitors.end(),
      [&](auto const& value) { return value.key == placement->resolved_monitor; });
  CHECK(monitor != local.environment.monitors.end());
  RECT frame{}; CHECK(GetWindowRect(h.MainHwnd(), &frame));
  CHECK(frame.left >= monitor->work_area.x);
}

void TestFullscreenNativePresentation() {
  Harness h;
  h.PumpModelAndApply();
  h.model_proxy.Invoke(h.model_catalog.id(), &WindowCatalog::AddWindow);
  h.PumpModelAndApply();
  h.PumpModelAndApply();
  auto& state = static_cast<CatalogUiState&>(*h.model_catalog->persisted.device_state);
  auto& holder = static_cast<WindowHolder&>(*h.ui_catalog->persisted.list->rows.front());
  auto& child = static_cast<ui::windows::WinDesktopWindowPresenter&>(*holder.window->presenter);
  std::array<ui::windows::WinDesktopWindowPresenter*, 2> const windows{h.main_presenter, &child};
  for (auto* presenter : windows) {
    HWND const native = presenter->hwnd();
    HWND const action = GetDlgItem(native, presenter->kFullscreenActionId);
    CHECK(action && IsWindowEnabled(action) && IsWindowVisible(action));
    // A real click can activate the target and legitimately publish window
    // order. Establish that separate input before measuring fullscreen itself.
    SetForegroundWindow(native);
    SetActiveWindow(native);
    SetFocus(action);
    DispatchPendingWin32Messages();
    h.PumpModelAndApply(); h.PumpModelAndApply();
    auto const normal_preference = *state.PlacementFor(presenter->window.id());
    WINDOWPLACEMENT original{sizeof(original)};
    CHECK(GetWindowPlacement(native, &original));
    LONG_PTR const style = GetWindowLongPtrW(native, GWL_STYLE);
    auto const user_generation = h.model_catalog->Generation();
    SendMessageW(action, BM_CLICK, 0, 0);
    CHECK(!state.IsWindowFullscreen(presenter->window.id()));
    CHECK(!presenter->IsPresentationFullscreen());
    h.PumpModelAndApply();
    h.PumpModelAndApply();
    CHECK(state.IsWindowFullscreen(presenter->window.id()));
    CHECK(presenter->IsPresentationFullscreen());
    CHECK((GetWindowLongPtrW(native, GWL_STYLE) & WS_CAPTION) == 0);
    RECT full{}; CHECK(GetWindowRect(native, &full));
    MONITORINFO monitor{sizeof(monitor)};
    CHECK(GetMonitorInfoW(MonitorFromWindow(native, MONITOR_DEFAULTTONEAREST), &monitor));
    CHECK(EqualRect(&full, &monitor.rcMonitor));
    auto const* preserved = state.PlacementFor(presenter->window.id());
    CHECK(preserved->frame_dip == normal_preference.frame_dip);
    CHECK(preserved->preferred_monitor == normal_preference.preferred_monitor);
    CHECK(h.model_catalog->Generation() == user_generation);
    // Scroll the content while the visible exit action remains fixed.
    auto const button_before = ReadChildInParentClient(action);
    SendMessageW(native, WM_VSCROLL, SB_BOTTOM, 0);
    auto const button_after = ReadChildInParentClient(action);
    CHECK(button_before.x == button_after.x && button_before.y == button_after.y);
    CHECK(IsWindowVisible(action));
    MSG escape{action, WM_KEYDOWN, VK_ESCAPE, 0};
    CHECK(presenter->HandlePresentationShortcut(escape));
    CHECK(presenter->IsPresentationFullscreen());
    h.PumpModelAndApply();
    h.PumpModelAndApply();
    CHECK(!presenter->IsPresentationFullscreen());
    CHECK(!state.IsWindowFullscreen(presenter->window.id()));
    CHECK((GetWindowLongPtrW(native, GWL_STYLE) & WS_OVERLAPPEDWINDOW) == (style & WS_OVERLAPPEDWINDOW));
    WINDOWPLACEMENT restored{sizeof(restored)};
    CHECK(GetWindowPlacement(native, &restored));
    CHECK(EqualRect(&restored.rcNormalPosition, &original.rcNormalPosition));
    CHECK(!presenter->HandlePresentationShortcut(escape));
    // Auto-repeat F11 is consumed without issuing another toggle; the next
    // ordinary keydown works from a focused native child as well.
    MSG f11{action, WM_KEYDOWN, VK_F11, static_cast<LPARAM>(1) << 30};
    auto const pending = h.model_work.size();
    CHECK(presenter->HandlePresentationShortcut(f11));
    CHECK(h.model_work.size() == pending);
    ShowWindow(native, SW_MAXIMIZE);
    f11.lParam = 0;
    CHECK(presenter->HandlePresentationShortcut(f11));
    h.PumpModelAndApply();
    CHECK(presenter->IsPresentationFullscreen());
    CHECK(!IsZoomed(native));
    CHECK(presenter->HandlePresentationShortcut(escape));
    h.PumpModelAndApply();
    CHECK(IsZoomed(native));
    ShowWindow(native, SW_RESTORE);
    h.PumpModelAndApply();
  }
  // A supplied translation becomes the standard native accessible name.
  SendMessageW(h.MainHwnd(), WM_COMMAND,
      MAKEWPARAM(example::surfaces::CatalogDesktopWindowPresenter::kLanguageFirst + 1, 0), 0);
  h.PumpModelAndApply();
  wchar_t caption[128]{};
  CHECK(GetWindowTextW(h.ControlHwnd(ui::windows::WinDesktopWindowPresenter::kFullscreenActionId), caption, 128));
  CHECK(std::wstring{caption} == L"На весь экран (F11)");
}

void TestKeyboardFocusStaysBelowFixedFullscreenAction() {
  Harness h;
  h.PumpModelAndApply();
  h.ResizeClient(300, 180, false);
  HWND const action = h.ControlHwnd(ui::windows::WinDesktopWindowPresenter::kFullscreenActionId);
  HWND const list = h.ControlHwnd(kListControlId);
  SetForegroundWindow(h.MainHwnd());
  SetActiveWindow(h.MainHwnd());
  SetFocus(action);
  SendMessageW(h.MainHwnd(), WM_VSCROLL, SB_BOTTOM, 0);
  auto const scrolled = ReadChildInParentClient(list);
  auto const toolbar = ReadChildInParentClient(action);
  CHECK(scrolled.y < toolbar.y + toolbar.height);
  SetFocus(list);
  CHECK(GetFocus() == list);
  DispatchPendingWin32Messages();
  auto const focused = ReadChildInParentClient(list);
  CHECK(focused.y >= toolbar.y + toolbar.height);
}

void TestFullscreenMonitorLossAndReturn() {
  using namespace ui;
  using namespace ui::windows;
  Harness h;
  h.PumpModelAndApply();
  WinUiEnvironment reader{h.MainHwnd()};
  auto const actual = reader.snapshot();
  if (actual.monitors.size() < 2) {
    std::cout << "fullscreen topology scope SKIP: two real monitors required\n";
    return;
  }
  auto primary = std::find_if(actual.monitors.begin(), actual.monitors.end(),
                             [](auto const& m) { return m.primary; });
  auto preferred = std::find_if(actual.monitors.begin(), actual.monitors.end(),
                               [](auto const& m) { return !m.primary; });
  CHECK(primary != actual.monitors.end() && preferred != actual.monitors.end());
  HWND const native = h.MainHwnd();
  SendMessageW(native, WM_ENTERSIZEMOVE, 0, 0);
  CHECK(SetWindowPos(native, nullptr, preferred->work_area.x + 60,
      preferred->work_area.y + 70, 700, 500, SWP_NOZORDER | SWP_NOACTIVATE));
  SendMessageW(native, WM_EXITSIZEMOVE, 0, 0);
  h.PumpModelAndApply(); h.PumpModelAndApply();
  auto& state = static_cast<CatalogUiState&>(*h.model_catalog->persisted.device_state);
  auto const normal = *state.PlacementFor(h.main_window_id);
  CHECK(normal.preferred_monitor == preferred->key);
  auto missing = actual;
  std::erase_if(missing.monitors, [&](auto const& m) { return m.key == preferred->key; });
  auto const check_fullscreen = [&](std::string const& key) {
    CHECK(h.main_presenter->IsPresentationFullscreen());
    CHECK(GetWinWindowMonitorKey(native) == key);
    RECT frame{}; CHECK(GetWindowRect(native, &frame));
    MONITORINFO info{sizeof(info)};
    CHECK(GetMonitorInfoW(MonitorFromWindow(native, MONITOR_DEFAULTTONEAREST), &info));
    CHECK(EqualRect(&frame, &info.rcMonitor));
    auto observed = std::find_if(state.windows.begin(), state.windows.end(),
        [&](auto const& window) { return window.window_id == h.main_window_id; });
    CHECK(observed != state.windows.end());
    CHECK(observed->monitor_key == key);
    CHECK(observed->dpi_x == GetWinWindowDpi(native));
    auto const* placement = state.PlacementFor(h.main_window_id);
    CHECK(placement->preferred_monitor == normal.preferred_monitor);
    CHECK(placement->resolved_monitor == key);
    CHECK(placement->frame_dip == normal.frame_dip);
  };
  HWND const action = GetDlgItem(native, WinDesktopWindowPresenter::kFullscreenActionId);
  SendMessageW(action, BM_CLICK, 0, 0);
  h.PumpModelAndApply(); h.PumpModelAndApply();
  check_fullscreen(preferred->key);
  // Only the model's observed topology changes; physical outputs stay enabled.
  state.ObserveEnvironment(missing, UiEnvironmentReason::kSystemNotification);
  h.PumpModelAndApply(); h.PumpModelAndApply();
  check_fullscreen(primary->key);
  state.ObserveEnvironment(actual, UiEnvironmentReason::kSystemNotification);
  h.PumpModelAndApply(); h.PumpModelAndApply();
  check_fullscreen(preferred->key);
  state.ObserveEnvironment(missing, UiEnvironmentReason::kSystemNotification);
  h.PumpModelAndApply(); h.PumpModelAndApply();
  check_fullscreen(primary->key);
  SendMessageW(action, BM_CLICK, 0, 0);
  h.PumpModelAndApply(); h.PumpModelAndApply();
  CHECK(!h.main_presenter->IsPresentationFullscreen());
  CHECK(GetWinWindowMonitorKey(native) == primary->key);
  CHECK(state.PlacementFor(h.main_window_id)->resolution == UiMonitorResolution::kMissing);
  CHECK(state.PlacementFor(h.main_window_id)->frame_dip == normal.frame_dip);
  state.ObserveEnvironment(actual, UiEnvironmentReason::kSystemNotification);
  h.PumpModelAndApply(); h.PumpModelAndApply();
  CHECK(GetWinWindowMonitorKey(native) == preferred->key);
  CHECK(state.PlacementFor(h.main_window_id)->frame_dip == normal.frame_dip);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureWindowCatalogGraphRegistration();
  apptraverse::test::TestFirstPublicationCarriesResolutionAfterNativeObservation();
  apptraverse::test::TestFullscreenNativePresentation();
  apptraverse::test::TestKeyboardFocusStaysBelowFixedFullscreenAction();
  apptraverse::test::TestFullscreenMonitorLossAndReturn();
  apptraverse::test::TestPrepareBindsQueueBeforeNativeOnLoad();
  apptraverse::test::TestBurstResizeWithoutPerStepPump();
  apptraverse::test::TestNativeAdaptiveChromeResizePath();
  apptraverse::test::TestLocalResizeDoesNotDependOnModelPump();
  apptraverse::test::TestRuntimeConstraintOverrideAndClear();
  apptraverse::test::TestSystemSettingsNotificationsStayLocal();
  apptraverse::test::TestPositionOnlyPublicationPreservesNativeControls();
  apptraverse::test::TestFiniteLayoutMaximumDoesNotRepeatedlyRedrawScrollbars();
  apptraverse::test::TestLocalAppearancePreferences();
  apptraverse::test::TestStartupObservationAndMonitorResolution();
  std::cout << "window_catalog_win32_chrome_adaptive_test OK\n";
  return 0;
}

#endif
