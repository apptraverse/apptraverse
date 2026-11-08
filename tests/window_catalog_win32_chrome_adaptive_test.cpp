#if !defined(_WIN32)
int main() { return 0; }
#else

#include <cstdlib>
#include <deque>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/graph_mirror.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/platform/windows/win_ui_frame.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "window_catalog_graph.h"
#include "window_catalog_chrome_layout.h"
#include "window_catalog_ids.h"
#include "window_catalog_prepare.h"
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

struct Harness {
  ae::RamDomainStorage model_storage;
  OverlayDomainStorage ui_storage{model_storage};
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};
  WindowCatalog::ptr model_catalog;
  WindowCatalog::ptr ui_catalog;
  std::deque<ModelObjectProxy::ModelWork> model_work;
  ModelObjectProxy model_proxy;
  ui::windows::WinDesktopWindowPresenter* main_presenter{nullptr};
  ae::ObjId const list_presenter_id;
  ae::ObjId const main_window_id;

  Harness()
      : model_proxy{[this](ModelObjectProxy::ModelWork work) {
          model_work.push_back(std::move(work));
        }},
        list_presenter_id{window_catalog_demo::ToObjId(
            window_catalog_demo::ObjId::CatalogListPresenter)},
        main_window_id{window_catalog_demo::ToObjId(
            window_catalog_demo::ObjId::MainWindow)} {
    EnsureObjectRegistration();
    EnsureWindowCatalogGraphRegistration();
    model_catalog = BuildWindowCatalogGraph(model_domain);
    PrepareWindowCatalogForRun(*model_catalog);
    auto ui_obj =
        CopyModelGraphToUiDomain(*model_catalog, ui_domain, ui_storage);
    ui_catalog =
        WindowCatalog::ptr::MakeFromThis(static_cast<WindowCatalog*>(ui_obj.get()));
    InitializePresenters(*ui_catalog, nullptr, &model_proxy);
    ui_catalog->persisted.main_window.Load();
    ui_catalog->persisted.main_window->presenter.Load();
    main_presenter = static_cast<ui::windows::WinDesktopWindowPresenter*>(
        ui_catalog->persisted.main_window->presenter.operator->());
    CHECK(main_presenter != nullptr);
    CHECK(main_presenter->hwnd() != nullptr);
  }

  ~Harness() { UnloadPresenters(*ui_catalog); }

  void PumpModelAndApply() {
    while (!model_work.empty()) {
      auto work = std::move(model_work.front());
      model_work.pop_front();
      work(model_domain);
    }
    ByteSink sink;
    SerializeStructuralNodePublication(*model_catalog, sink);
    ByteSource in;
    in.data = sink.bytes.data();
    in.size = sink.bytes.size();
    ApplyStructuralPublicationAndUpdatePresenters(
        in, ui_domain, ui_storage, *ui_catalog, nullptr, &model_proxy);
  }

  HWND MainHwnd() const { return main_presenter->hwnd(); }

  HWND ControlHwnd(std::uint32_t control_id) const {
    return GetDlgItem(MainHwnd(), static_cast<int>(control_id));
  }

  void ResizeClient(std::int32_t w, std::int32_t h, bool interactive) {
    SetMainClientSize(MainHwnd(), w, h, interactive);
    PumpModelAndApply();
  }
};

void ExpectWideChrome(HWND main, std::int32_t client_w, std::int32_t client_h) {
  HWND const list = GetDlgItem(main, static_cast<int>(kListControlId));
  HWND const add = GetDlgItem(main, static_cast<int>(kAddControlId));
  HWND const open = GetDlgItem(main, static_cast<int>(kOpenControlId));
  HWND const del = GetDlgItem(main, static_cast<int>(kDeleteControlId));
  HWND const lang = GetDlgItem(main, static_cast<int>(kLanguageControlId));
  CHECK(list != nullptr);
  CHECK(add != nullptr);
  CHECK(open != nullptr);
  CHECK(del != nullptr);
  CHECK(lang != nullptr);
  ClientRect const list_r = ReadChildInParentClient(list);
  ClientRect const add_r = ReadChildInParentClient(add);
  CHECK(list_r.x < add_r.x);
  CHECK(!RectsOverlap(list_r, add_r));
  CHECK(list_r.x >= 0);
  CHECK(list_r.y >= 0);
  CHECK(add_r.x + add_r.width <= client_w);
  ClientRect const lang_r = ReadChildInParentClient(lang);
  CHECK(lang_r.y + lang_r.height <= client_h);
  auto const layout = ComputeWindowCatalogMainChromeLayout(client_w, client_h, 160);
  CHECK(layout.list.x < layout.add.x);
  CHECK(list_r.x == layout.list.x);
  CHECK(list_r.y == layout.list.y);
  CHECK(add_r.x == layout.add.x);
}

void ExpectTallChrome(HWND main, std::int32_t client_w, std::int32_t client_h) {
  HWND const list = GetDlgItem(main, static_cast<int>(kListControlId));
  HWND const add = GetDlgItem(main, static_cast<int>(kAddControlId));
  CHECK(list != nullptr);
  CHECK(add != nullptr);
  ClientRect const list_r = ReadChildInParentClient(list);
  ClientRect const add_r = ReadChildInParentClient(add);
  CHECK(list_r.y < add_r.y);
  CHECK(!RectsOverlap(list_r, add_r));
  auto const layout = ComputeWindowCatalogMainChromeLayout(client_w, client_h, 160);
  CHECK(layout.list.y + layout.list.height <= layout.add.y);
  CHECK(list_r.x == layout.list.x);
  CHECK(add_r.y == layout.add.y);
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

  h.ResizeClient(900, 500, false);
  ExpectWideChrome(main, 900, 500);
  CHECK(h.model_catalog->persisted.main_client_viewport.width == 900);
  CHECK(h.model_catalog->persisted.main_client_viewport.height == 500);

  SetMainClientSize(main, 500, 900, true);
  h.PumpModelAndApply();
  ExpectTallChrome(main, 500, 900);
  EndInteractive(main);
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

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::test::TestNativeAdaptiveChromeResizePath();
  std::cout << "window_catalog_win32_chrome_adaptive_test OK\n";
  return 0;
}

#endif
