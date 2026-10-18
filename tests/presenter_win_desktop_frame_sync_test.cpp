#if !defined(_WIN32)
int main() { return 0; }
#else

#include <cstdlib>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_serialization.h"
// ApplyStructuralPublicationAndUpdatePresenters
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

ae::ObjId const kRootId{0x73000000u};
ae::ObjId const kWindowId{0x73000001u};
ae::ObjId const kPresenterId{0x73000002u};

class FrameSyncRoot : public NodeFor<FrameSyncRoot> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::FrameSyncRoot", FrameSyncRoot,
                           Node, 0)

 protected:
  FrameSyncRoot() = default;

 public:
  explicit FrameSyncRoot(ae::ObjProp prop) : NodeFor{prop} {}

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

APPTRAVERSE_REGISTER(FrameSyncRoot);

void ApplyFrameToMirror(ui::UiDesktopWindow& desktop, std::int32_t x,
                        std::int32_t y, std::int32_t cw, std::int32_t ch) {
  auto event = ui::SetUiDesktopWindowFrameEvent::ptr::Create(
      ae::CreateWith{*desktop.domain});
  event->x = x;
  event->y = y;
  event->client_width = cw;
  event->client_height = ch;
  desktop.Apply(*event);
}

void ReadHwndFrame(HWND hwnd, std::int32_t& x, std::int32_t& y,
                   std::int32_t& cw, std::int32_t& ch) {
  ui::windows::ReadModelFrameFromHwnd(hwnd, x, y, cw, ch);
}

struct Harness {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  FrameSyncRoot::ptr root;
  ui::windows::WinDesktopWindowPresenter* presenter{nullptr};

  Harness() {
    EnsureObjectRegistration();
    ui::windows::EnsureWinUiPresenterRegistration();
    (void)&g_apptraverse_registrar_FrameSyncRoot;

    root = FrameSyncRoot::ptr::Create(ae::CreateWith{domain}.with_id(kRootId));
    InitializeRuntimeNode(*root);
    root->window = ui::UiDesktopWindow::ptr::Create(
        ae::CreateWith{domain}.with_id(kWindowId));
    InitializeRuntimeNode(*root->window, *root);
    root->window_presenter =
        ui::windows::WinDesktopWindowPresenter::ptr::Create(
            ae::CreateWith{domain}.with_id(kPresenterId));
    root->window_presenter->window = root->window;

    InitializePresenters(*root, nullptr, nullptr);
    presenter = &*root->window_presenter;
    CHECK(presenter->hwnd() != nullptr);
    ui::windows::WinDesktopWindowPresenter::TestResetSetWindowPosCount();
  }
};

void TestStalePublicationDuringDrag() {
  Harness h;
  HWND const hwnd = h.presenter->hwnd();
  h.presenter->TestSetInteractiveMoveActive(true);

  h.presenter->TestMoveNativeOuter(300, 200, 640, 480);
  ApplyFrameToMirror(*h.root->window, 100, 100, 640, 480);
  ui::windows::WinDesktopWindowPresenter::TestResetSetWindowPosCount();
  h.presenter->OnModelChanged();

  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t cw = 0;
  std::int32_t ch = 0;
  ReadHwndFrame(hwnd, x, y, cw, ch);
  CHECK(x == 300);
  CHECK(y == 200);
  CHECK(ui::windows::WinDesktopWindowPresenter::TestSetWindowPosCount() == 0);

  h.presenter->TestSetInteractiveMoveActive(false);
  ApplyFrameToMirror(*h.root->window, 300, 200, 640, 480);
  h.presenter->OnModelChanged();
  ReadHwndFrame(hwnd, x, y, cw, ch);
  CHECK(x == 300);
  CHECK(y == 200);
}

void TestStalePublicationAfterRelease() {
  Harness h;
  HWND const hwnd = h.presenter->hwnd();
  h.presenter->TestMoveNativeOuter(400, 250, 640, 480);
  h.presenter->TestMoveNativeOuter(500, 350, 640, 480);

  ApplyFrameToMirror(*h.root->window, 400, 250, 640, 480);
  ui::windows::WinDesktopWindowPresenter::TestResetSetWindowPosCount();
  h.presenter->OnModelChanged();
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t cw = 0;
  std::int32_t ch = 0;
  ReadHwndFrame(hwnd, x, y, cw, ch);
  CHECK(x == 500);
  CHECK(y == 350);
  CHECK(ui::windows::WinDesktopWindowPresenter::TestSetWindowPosCount() == 0);

  ApplyFrameToMirror(*h.root->window, 500, 350, 640, 480);
  h.presenter->OnModelChanged();
  ReadHwndFrame(hwnd, x, y, cw, ch);
  CHECK(x == 500);
  CHECK(y == 350);
  CHECK(ui::windows::WinDesktopWindowPresenter::TestSetWindowPosCount() == 0);
}

void ApplyModelStructuralPublication(Harness& h) {
  ByteSink sink;
  SerializeStructuralNodePublication(*h.root->window, sink);
  ByteSource in;
  in.data = sink.bytes.data();
  in.size = sink.bytes.size();
  ApplyStructuralPublicationAndUpdatePresenters(in, h.domain, h.storage, *h.root,
                                                nullptr, nullptr);
}

void TestProgrammaticMoveAfterSync() {
  Harness h;
  HWND const hwnd = h.presenter->hwnd();
  h.presenter->TestMoveNativeOuter(120, 130, 640, 480);
  ApplyFrameToMirror(*h.root->window, 120, 130, 640, 480);
  h.presenter->OnModelChanged();

  h.root->window->RequestFrame(700, 800, 640, 480);
  ui::windows::WinDesktopWindowPresenter::TestResetSetWindowPosCount();
  ApplyModelStructuralPublication(h);
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t cw = 0;
  std::int32_t ch = 0;
  ReadHwndFrame(hwnd, x, y, cw, ch);
  CHECK(x == 700);
  CHECK(y == 800);
  CHECK(ui::windows::WinDesktopWindowPresenter::TestSetWindowPosCount() == 1);
}

void TestNonGeometryPublicationDoesNotMove() {
  Harness h;
  HWND const hwnd = h.presenter->hwnd();
  h.presenter->TestMoveNativeOuter(150, 160, 640, 480);
  ApplyFrameToMirror(*h.root->window, 150, 160, 640, 480);
  h.presenter->OnModelChanged();
  std::int32_t x0 = 0;
  std::int32_t y0 = 0;
  std::int32_t cw0 = 0;
  std::int32_t ch0 = 0;
  ReadHwndFrame(hwnd, x0, y0, cw0, ch0);
  ui::windows::WinDesktopWindowPresenter::TestResetSetWindowPosCount();
  h.presenter->OnModelChanged();
  CHECK(ui::windows::WinDesktopWindowPresenter::TestSetWindowPosCount() == 0);
}

void TestSecondDragBeforeFirstAck() {
  Harness h;
  HWND const hwnd = h.presenter->hwnd();
  h.presenter->TestMoveNativeOuter(10, 10, 640, 480);
  ApplyFrameToMirror(*h.root->window, 10, 10, 640, 480);
  h.presenter->OnModelChanged();
  h.presenter->TestMoveNativeOuter(20, 30, 640, 480);
  ApplyFrameToMirror(*h.root->window, 10, 10, 640, 480);
  ui::windows::WinDesktopWindowPresenter::TestResetSetWindowPosCount();
  h.presenter->OnModelChanged();
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t cw = 0;
  std::int32_t ch = 0;
  ReadHwndFrame(hwnd, x, y, cw, ch);
  CHECK(x == 20);
  CHECK(y == 30);
  CHECK(ui::windows::WinDesktopWindowPresenter::TestSetWindowPosCount() == 0);
}

void TestAbaStaleMiddlePublicationDoesNotJump() {
  Harness h;
  HWND const hwnd = h.presenter->hwnd();
  h.presenter->TestMoveNativeOuter(10, 10, 640, 480);
  ApplyFrameToMirror(*h.root->window, 10, 10, 640, 480);
  h.presenter->TestMoveNativeOuter(20, 20, 640, 480);
  ApplyFrameToMirror(*h.root->window, 20, 20, 640, 480);
  h.presenter->TestMoveNativeOuter(10, 10, 640, 480);
  ui::windows::WinDesktopWindowPresenter::TestResetSetWindowPosCount();
  ApplyFrameToMirror(*h.root->window, 20, 20, 640, 480);
  h.presenter->OnModelChanged();
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t cw = 0;
  std::int32_t ch = 0;
  ReadHwndFrame(hwnd, x, y, cw, ch);
  CHECK(x == 10);
  CHECK(y == 10);
  CHECK(ui::windows::WinDesktopWindowPresenter::TestSetWindowPosCount() == 0);
}

void TestProgrammaticMoveWhileNativePending() {
  Harness h;
  HWND const hwnd = h.presenter->hwnd();
  h.presenter->TestMoveNativeOuter(50, 50, 640, 480);
  ApplyFrameToMirror(*h.root->window, 50, 50, 640, 480);
  h.presenter->OnModelChanged();
  h.presenter->TestMoveNativeOuter(60, 60, 640, 480);
  ui::windows::WinDesktopWindowPresenter::TestResetSetWindowPosCount();
  h.root->window->RequestFrame(700, 800, 640, 480);
  ApplyModelStructuralPublication(h);
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t cw = 0;
  std::int32_t ch = 0;
  ReadHwndFrame(hwnd, x, y, cw, ch);
  CHECK(x == 700);
  CHECK(y == 800);
  CHECK(ui::windows::WinDesktopWindowPresenter::TestSetWindowPosCount() == 1);
}

void TestInteractiveDragCoalescesModelSubmits() {
  Harness h;
  h.presenter->TestResetModelFrameSubmitCount();
  for (int i = 0; i < 500; ++i) {
    h.presenter->TestSimulateInteractiveNativeMove(100 + i, 200 + i, 640, 480);
  }
  CHECK(h.presenter->TestModelFrameSubmitCount() == 0);
  h.presenter->TestFinishInteractiveNativeMove();
  CHECK(h.presenter->TestModelFrameSubmitCount() == 1);
}

void TestResizeStaleEcho() {
  Harness h;
  HWND const hwnd = h.presenter->hwnd();
  h.presenter->TestSetInteractiveMoveActive(true);
  h.presenter->TestMoveNativeOuter(180, 190, 720, 520);
  ApplyFrameToMirror(*h.root->window, 180, 190, 640, 480);
  ui::windows::WinDesktopWindowPresenter::TestResetSetWindowPosCount();
  h.presenter->OnModelChanged();
  std::int32_t cw = 0;
  std::int32_t ch = 0;
  std::int32_t x = 0;
  std::int32_t y = 0;
  ReadHwndFrame(hwnd, x, y, cw, ch);
  CHECK(cw == 720);
  CHECK(ch == 520);
  CHECK(ui::windows::WinDesktopWindowPresenter::TestSetWindowPosCount() == 0);
  h.presenter->TestSetInteractiveMoveActive(false);
  ApplyFrameToMirror(*h.root->window, 180, 190, 720, 520);
  h.presenter->OnModelChanged();
  ReadHwndFrame(hwnd, x, y, cw, ch);
  CHECK(cw == 720);
  CHECK(ch == 520);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::test::TestStalePublicationDuringDrag();
  apptraverse::test::TestStalePublicationAfterRelease();
  apptraverse::test::TestProgrammaticMoveAfterSync();
  apptraverse::test::TestNonGeometryPublicationDoesNotMove();
  apptraverse::test::TestSecondDragBeforeFirstAck();
  apptraverse::test::TestInteractiveDragCoalescesModelSubmits();
  apptraverse::test::TestAbaStaleMiddlePublicationDoesNotJump();
  apptraverse::test::TestProgrammaticMoveWhileNativePending();
  apptraverse::test::TestResizeStaleEcho();
  std::cout << "presenter_win_desktop_frame_sync_test OK\n";
  return 0;
}

#endif
