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
#include "apptraverse/ui/ui_locale_settings.h"

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

ae::ObjId const kRootId{0x74000000u};
ae::ObjId const kWindowId{0x74000001u};
ae::ObjId const kPresenterId{0x74000002u};
ae::ObjId const kLocaleId{0x74000003u};

class FramePubRoot : public NodeFor<FramePubRoot> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::FramePubRoot", FramePubRoot,
                           Node, 0)

 protected:
  FramePubRoot() = default;

 public:
  explicit FramePubRoot(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window), AE_MMBR(locale), AE_MMBR(window_presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, window, locale, window_presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, window, locale, window_presenter);
  }

  ui::UiDesktopWindow::ptr window;
  ui::UiLocaleSettings::ptr locale;
  ui::windows::WinDesktopWindowPresenter::ptr window_presenter;
};

APPTRAVERSE_REGISTER(FramePubRoot);

void ReadHwndFrame(HWND hwnd, std::int32_t& x, std::int32_t& y,
                   std::int32_t& cw, std::int32_t& ch) {
  ui::windows::ReadModelFrameFromHwnd(hwnd, x, y, cw, ch);
}

void InteractiveDragEnd(ui::windows::WinDesktopWindowPresenter& presenter,
                        std::int32_t x, std::int32_t y, std::int32_t cw,
                        std::int32_t ch) {
  HWND const hwnd = presenter.hwnd();
  CHECK(hwnd != nullptr);
  SendMessageW(hwnd, WM_ENTERSIZEMOVE, 0, 0);
  int outer_w = 0;
  int outer_h = 0;
  ui::windows::ClientSizeToOuter(cw, ch, outer_w, outer_h);
  CHECK(SetWindowPos(hwnd, nullptr, x, y, outer_w, outer_h,
                     SWP_NOZORDER | SWP_NOACTIVATE) != 0);
  SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);
}

struct E2EHarness {
  ae::RamDomainStorage model_storage;
  OverlayDomainStorage ui_storage{model_storage};
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};
  FramePubRoot::ptr model_root;
  FramePubRoot::ptr ui_root;
  ui::windows::WinDesktopWindowPresenter* presenter{nullptr};
  std::deque<ModelObjectProxy::ModelWork> deferred_model_work;
  ModelObjectProxy model_proxy;

  E2EHarness()
      : model_proxy{[this](ModelObjectProxy::ModelWork work) {
          deferred_model_work.push_back(std::move(work));
        }} {
    EnsureObjectRegistration();
    ui::windows::EnsureWinUiPresenterRegistration();
    (void)&g_apptraverse_registrar_FramePubRoot;

    model_root = FramePubRoot::ptr::Create(
        ae::CreateWith{model_domain}.with_id(kRootId));
    InitializeRuntimeNode(*model_root);
    model_root->window = ui::UiDesktopWindow::ptr::Create(
        ae::CreateWith{model_domain}.with_id(kWindowId));
    InitializeRuntimeNode(*model_root->window, *model_root);
    model_root->locale = ui::UiLocaleSettings::ptr::Create(
        ae::CreateWith{model_domain}.with_id(kLocaleId));
    InitializeRuntimeNode(*model_root->locale, *model_root);

    auto ui_obj = CopyModelGraphToUiDomain(*model_root, ui_domain, ui_storage);
    ui_root = FramePubRoot::ptr::MakeFromThis(
        static_cast<FramePubRoot*>(ui_obj.get()));
    ui_root->window_presenter =
        ui::windows::WinDesktopWindowPresenter::ptr::Create(
            ae::CreateWith{ui_domain}.with_id(kPresenterId));
    ui_root->window_presenter->window = ui_root->window;
    ui_root->window_presenter->locale = ui_root->locale;

    InitializePresenters(*ui_root, nullptr, &model_proxy);
    presenter = &*ui_root->window_presenter;
    CHECK(presenter->hwnd() != nullptr);
    ui::windows::WinDesktopWindowPresenter::TestResetSetWindowPosCount();
  }

  ~E2EHarness() { UnloadPresenters(*ui_root); }

  void PumpModel() {
    while (!deferred_model_work.empty()) {
      auto work = std::move(deferred_model_work.front());
      deferred_model_work.pop_front();
      work(model_domain);
    }
  }

  std::vector<std::uint8_t> CaptureStructuralPublication() {
    ByteSink sink;
    SerializeStructuralNodePublication(*model_root->window, sink);
    return sink.bytes;
  }

  void ApplyStructuralBytes(std::vector<std::uint8_t> const& bytes) {
    ByteSource in;
    in.data = bytes.data();
    in.size = bytes.size();
    ApplyStructuralPublicationAndUpdatePresenters(
        in, ui_domain, ui_storage, *ui_root, nullptr, &model_proxy);
  }

  void MoveCommitPublish(std::int32_t x, std::int32_t y, std::int32_t cw,
                         std::int32_t ch) {
    InteractiveDragEnd(*presenter, x, y, cw, ch);
    PumpModel();
  }
};

void TestAbaViaPublicationChannel() {
  E2EHarness h;
  HWND const hwnd = h.presenter->hwnd();
  h.MoveCommitPublish(10, 10, 640, 480);
  auto const pub_a1 = h.CaptureStructuralPublication();
  h.MoveCommitPublish(20, 20, 640, 480);
  auto const pub_b = h.CaptureStructuralPublication();
  h.MoveCommitPublish(10, 10, 640, 480);
  auto const pub_a2 = h.CaptureStructuralPublication();

  ui::windows::WinDesktopWindowPresenter::TestResetSetWindowPosCount();
  h.ApplyStructuralBytes(pub_a1);
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t cw = 0;
  std::int32_t ch = 0;
  ReadHwndFrame(hwnd, x, y, cw, ch);
  CHECK(x == 10);
  CHECK(y == 10);

  h.ApplyStructuralBytes(pub_b);
  ReadHwndFrame(hwnd, x, y, cw, ch);
  CHECK(x == 10);
  CHECK(y == 10);
  CHECK(ui::windows::WinDesktopWindowPresenter::TestSetWindowPosCount() == 0);

  h.ApplyStructuralBytes(pub_a2);
  ReadHwndFrame(hwnd, x, y, cw, ch);
  CHECK(x == 10);
  CHECK(y == 10);
  CHECK(h.ui_root->window->x() == 10);
  CHECK(h.ui_root->window->y() == 10);
}

void TestDelayedNativeReportPublicationsAbc() {
  E2EHarness h;
  HWND const hwnd = h.presenter->hwnd();

  auto capture_after_drag = [&](std::int32_t x, std::int32_t y) {
    InteractiveDragEnd(*h.presenter, x, y, 640, 480);
    h.PumpModel();
    return h.CaptureStructuralPublication();
  };

  auto const pub_a = capture_after_drag(10, 10);
  auto const pub_b = capture_after_drag(20, 20);
  auto const pub_c = capture_after_drag(30, 30);

  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t cw = 0;
  std::int32_t ch = 0;
  ReadHwndFrame(hwnd, x, y, cw, ch);
  CHECK(x == 30);
  CHECK(y == 30);

  ui::windows::WinDesktopWindowPresenter::TestResetSetWindowPosCount();
  h.ApplyStructuralBytes(pub_a);
  ReadHwndFrame(hwnd, x, y, cw, ch);
  CHECK(x == 30);
  CHECK(y == 30);

  h.ApplyStructuralBytes(pub_b);
  ReadHwndFrame(hwnd, x, y, cw, ch);
  CHECK(x == 30);
  CHECK(y == 30);

  h.ApplyStructuralBytes(pub_c);
  ReadHwndFrame(hwnd, x, y, cw, ch);
  CHECK(x == 30);
  CHECK(y == 30);
  CHECK(ui::windows::WinDesktopWindowPresenter::TestSetWindowPosCount() == 0);
  CHECK(h.ui_root->window->x() == 30);
  CHECK(h.ui_root->window->y() == 30);
  CHECK(h.model_root->window->x() == 30);
  CHECK(h.model_root->window->y() == 30);
}

void TestProgrammaticFrameViaModelCommit() {
  E2EHarness h;
  HWND const hwnd = h.presenter->hwnd();
  h.MoveCommitPublish(50, 50, 640, 480);
  h.PumpModel();
  h.model_root->window->RequestFrame(700, 800, 640, 480);
  auto const pub = h.CaptureStructuralPublication();
  ui::windows::WinDesktopWindowPresenter::TestResetSetWindowPosCount();
  h.ApplyStructuralBytes(pub);
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t cw = 0;
  std::int32_t ch = 0;
  ReadHwndFrame(hwnd, x, y, cw, ch);
  CHECK(x == 700);
  CHECK(y == 800);
  CHECK(ui::windows::WinDesktopWindowPresenter::TestSetWindowPosCount() == 1);

  ui::windows::WinDesktopWindowPresenter::TestResetSetWindowPosCount();
  h.ApplyStructuralBytes(h.CaptureStructuralPublication());
  CHECK(ui::windows::WinDesktopWindowPresenter::TestSetWindowPosCount() == 0);
}

void TestLocalePublicationDoesNotMoveWindow() {
  E2EHarness h;
  HWND const hwnd = h.presenter->hwnd();
  h.MoveCommitPublish(150, 160, 640, 480);
  h.ApplyStructuralBytes(h.CaptureStructuralPublication());
  std::int32_t x0 = 0;
  std::int32_t y0 = 0;
  std::int32_t cw0 = 0;
  std::int32_t ch0 = 0;
  ReadHwndFrame(hwnd, x0, y0, cw0, ch0);
  auto ev = ui::SetUiLanguageIndexEvent::ptr::Create(
      ae::CreateWith{h.model_domain});
  ev->language_index = 1;
  h.model_root->locale->Commit(std::move(ev));
  ByteSink sink;
  SerializeStructuralNodePublication(*h.model_root->locale, sink);
  ui::windows::WinDesktopWindowPresenter::TestResetSetWindowPosCount();
  h.ApplyStructuralBytes(sink.bytes);
  CHECK(ui::windows::WinDesktopWindowPresenter::TestSetWindowPosCount() == 0);
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t cw = 0;
  std::int32_t ch = 0;
  ReadHwndFrame(hwnd, x, y, cw, ch);
  CHECK(x == x0);
  CHECK(y == y0);
  CHECK(h.ui_root->locale->language_index() == 1);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::test::TestAbaViaPublicationChannel();
  apptraverse::test::TestDelayedNativeReportPublicationsAbc();
  apptraverse::test::TestProgrammaticFrameViaModelCommit();
  apptraverse::test::TestLocalePublicationDoesNotMoveWindow();
  std::cout << "presenter_win_desktop_frame_publication_e2e_test OK\n";
  return 0;
}

#endif
