#if !defined(_WIN32)
int main() { return 0; }
#else

#include <cstdlib>
#include <deque>
#include <iostream>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/graph_mirror.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/platform/windows/win_ui_frame.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_desktop_window.h"

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

ae::ObjId const kRootId{0x75000000u};
ae::ObjId const kWindowId{0x75000001u};
ae::ObjId const kPresenterId{0x75000002u};

class DragRoot : public NodeFor<DragRoot> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::DragRoot", DragRoot, Node, 0)

 protected:
  DragRoot() = default;

 public:
  explicit DragRoot(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window), AE_MMBR(window_presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, window, window_presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, window, window_presenter);
  }

  ui::UiDesktopWindow::ptr window;
  ui::windows::WinDesktopWindowPresenter::ptr window_presenter;
};

APPTRAVERSE_REGISTER(DragRoot);

void ReadHwndFrame(HWND hwnd, std::int32_t& x, std::int32_t& y,
                   std::int32_t& cw, std::int32_t& ch) {
  ui::windows::ReadModelFrameFromHwnd(hwnd, x, y, cw, ch);
}

void SetHwndOuter(HWND hwnd, std::int32_t x, std::int32_t y, std::int32_t cw,
                  std::int32_t ch) {
  int outer_w = 0;
  int outer_h = 0;
  ui::windows::ClientSizeToOuter(cw, ch, outer_w, outer_h);
  CHECK(SetWindowPos(hwnd, nullptr, x, y, outer_w, outer_h,
                     SWP_NOZORDER | SWP_NOACTIVATE) != 0);
}

void InteractiveDragEnd(ui::windows::WinDesktopWindowPresenter& presenter,
                          std::int32_t x, std::int32_t y, std::int32_t cw,
                          std::int32_t ch) {
  HWND const hwnd = presenter.hwnd();
  CHECK(hwnd != nullptr);
  SendMessageW(hwnd, WM_ENTERSIZEMOVE, 0, 0);
  SetHwndOuter(hwnd, x, y, cw, ch);
  SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);
}

struct Harness {
  ae::RamDomainStorage model_storage;
  OverlayDomainStorage ui_storage{model_storage};
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};
  DragRoot::ptr model_root;
  DragRoot::ptr ui_root;
  ui::windows::WinDesktopWindowPresenter* presenter{nullptr};
  std::deque<ModelObjectProxy::ModelWork> deferred_model_work;
  ModelObjectProxy model_proxy;

  Harness()
      : model_proxy{[this](ModelObjectProxy::ModelWork work) {
          deferred_model_work.push_back(std::move(work));
        }} {
    EnsureObjectRegistration();
    ui::windows::EnsureWinUiPresenterRegistration();
    (void)&g_apptraverse_registrar_DragRoot;

    model_root = DragRoot::ptr::Create(ae::CreateWith{model_domain}.with_id(kRootId));
    InitializeRuntimeNode(*model_root);
    model_root->window = ui::UiDesktopWindow::ptr::Create(
        ae::CreateWith{model_domain}.with_id(kWindowId));
    InitializeRuntimeNode(*model_root->window, *model_root);

    auto ui_obj = CopyModelGraphToUiDomain(*model_root, ui_domain, ui_storage);
    ui_root = DragRoot::ptr::MakeFromThis(static_cast<DragRoot*>(ui_obj.get()));
    ui_root->window_presenter =
        ui::windows::WinDesktopWindowPresenter::ptr::Create(
            ae::CreateWith{ui_domain}.with_id(kPresenterId));
    ui_root->window_presenter->window = ui_root->window;

    InitializePresenters(*ui_root, nullptr, &model_proxy);
    presenter = &*ui_root->window_presenter;
    CHECK(presenter->hwnd() != nullptr);
  }

  ~Harness() { UnloadPresenters(*ui_root); }

  void PumpModel() {
    while (!deferred_model_work.empty()) {
      auto work = std::move(deferred_model_work.front());
      deferred_model_work.pop_front();
      work(model_domain);
    }
  }

  void PumpOneModelTask() {
    CHECK(!deferred_model_work.empty());
    auto work = std::move(deferred_model_work.front());
    deferred_model_work.pop_front();
    work(model_domain);
  }

  void ApplyWindowPublication() {
    ByteSink sink;
    SerializeStructuralNodePublication(*model_root->window, sink);
    ByteSource in;
    in.data = sink.bytes.data();
    in.size = sink.bytes.size();
    ApplyStructuralPublication(in, ui_domain, ui_storage);
    presenter->OnModelChanged();
  }

  void SyncModelUiAt(std::int32_t x, std::int32_t y) {
    InteractiveDragEnd(*presenter, x, y, 640, 480);
    PumpModel();
    ApplyWindowPublication();
  }
};

void TestTwoDragsBackToAWithLaggyMirror() {
  Harness h;
  HWND const hwnd = h.presenter->hwnd();
  h.SyncModelUiAt(100, 100);
  h.presenter->TestResetModelFrameSubmitCount();
  std::uint64_t const serial0 = h.presenter->TestNativeFrameSubmitSerial();

  InteractiveDragEnd(*h.presenter, 200, 200, 640, 480);
  CHECK(h.presenter->TestModelFrameSubmitCount() == 1);
  CHECK(h.presenter->TestNativeFrameSubmitSerial() == serial0 + 1);
  CHECK(h.deferred_model_work.size() == 1);

  InteractiveDragEnd(*h.presenter, 100, 100, 640, 480);
  CHECK(h.presenter->TestModelFrameSubmitCount() == 2);
  CHECK(h.presenter->TestNativeFrameSubmitSerial() == serial0 + 2);
  CHECK(h.deferred_model_work.size() == 2);

  h.PumpOneModelTask();
  ByteSink pub_b_sink;
  SerializeStructuralNodePublication(*h.model_root->window, pub_b_sink);
  auto const pub_b = pub_b_sink.bytes;
  CHECK(h.model_root->window->x() == 200);
  CHECK(h.model_root->window->y() == 200);

  h.PumpModel();
  CHECK(h.model_root->window->x() == 100);
  CHECK(h.model_root->window->y() == 100);

  ui::windows::WinDesktopWindowPresenter::TestResetSetWindowPosCount();
  ByteSource stale;
  stale.data = pub_b.data();
  stale.size = pub_b.size();
  ApplyStructuralPublication(stale, h.ui_domain, h.ui_storage);
  h.presenter->OnModelChanged();
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t cw = 0;
  std::int32_t ch = 0;
  ReadHwndFrame(hwnd, x, y, cw, ch);
  CHECK(x == 100);
  CHECK(y == 100);
  CHECK(ui::windows::WinDesktopWindowPresenter::TestSetWindowPosCount() == 0);

  h.ApplyWindowPublication();
  ReadHwndFrame(hwnd, x, y, cw, ch);
  CHECK(x == 100);
  CHECK(y == 100);
  CHECK(h.ui_root->window->x() == 100);
  CHECK(h.ui_root->window->y() == 100);
}

void TestInteractiveDragDoesNotSubmitIntermediateSteps() {
  Harness h;
  h.presenter->TestResetModelFrameSubmitCount();
  SendMessageW(h.presenter->hwnd(), WM_ENTERSIZEMOVE, 0, 0);
  for (int i = 0; i < 200; ++i) {
    SetHwndOuter(h.presenter->hwnd(), 50 + i, 60 + i, 640, 480);
  }
  CHECK(h.presenter->TestModelFrameSubmitCount() == 0);
  SendMessageW(h.presenter->hwnd(), WM_EXITSIZEMOVE, 0, 0);
  CHECK(h.presenter->TestModelFrameSubmitCount() == 1);
}

void TestSequentialInteractiveDragsIncrementSerial() {
  Harness h;
  h.SyncModelUiAt(10, 10);
  h.presenter->TestResetModelFrameSubmitCount();
  std::uint64_t const serial0 = h.presenter->TestNativeFrameSubmitSerial();
  InteractiveDragEnd(*h.presenter, 30, 30, 640, 480);
  InteractiveDragEnd(*h.presenter, 10, 10, 640, 480);
  CHECK(h.presenter->TestModelFrameSubmitCount() == 2);
  CHECK(h.presenter->TestNativeFrameSubmitSerial() == serial0 + 2);
  h.PumpModel();
  CHECK(h.model_root->window->x() == 10);
  CHECK(h.model_root->window->y() == 10);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::test::TestInteractiveDragDoesNotSubmitIntermediateSteps();
  apptraverse::test::TestSequentialInteractiveDragsIncrementSerial();
  apptraverse::test::TestTwoDragsBackToAWithLaggyMirror();
  std::cout << "presenter_win_desktop_interactive_drag_e2e_test OK\n";
  return 0;
}

#endif
