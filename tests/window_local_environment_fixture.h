#pragma once
#include "window_title_fixture.h"
#include "apptraverse/distill.h"
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include "apptraverse/application_ui_session.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/platform/windows/win_ui_environment.h"
#include "apptraverse/platform/windows/win_ui_frame.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
namespace apptraverse::test {
#define LOCAL_CHECK(c) do { if (!(c)) { std::cerr << "CHECK " #c << " line " << __LINE__ << '\n'; std::exit(1); } } while (false)
class NativeLocalWindowRoot : public NodeFor<NativeLocalWindowRoot> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::NativeLocalWindowRoot", NativeLocalWindowRoot, Node, 0)
 protected: NativeLocalWindowRoot() = default;
 public:
  explicit NativeLocalWindowRoot(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(window), AE_MMBR(second_window))
  template<class D> void Load(ae::Version<0>, D& d) { d(base_, window, second_window); }
  template<class D> void Save(ae::Version<0>, D& d) const { d(base_, window, second_window); }
  ui::UiDesktopWindow::ptr window;
  ui::UiDesktopWindow::ptr second_window;
};
APPTRAVERSE_REGISTER(NativeLocalWindowRoot);
struct NativeLocalWindowHarness {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  NativeLocalWindowRoot::ptr model;
  std::deque<ModelObjectProxy::ModelWork> pending;
  ModelObjectProxy proxy{[this](auto work) { pending.push_back(std::move(work)); }};
  std::unique_ptr<ApplicationUiSession> session;
  ui::windows::WinDesktopWindowPresenter* presenter{};
  explicit NativeLocalWindowHarness(bool two_windows = false) {
    EnsureCoreObjectRegistration(); ui::windows::EnsureWinUiPresenterRegistration();
    (void)&g_apptraverse_registrar_NativeLocalWindowRoot;
    model = NativeLocalWindowRoot::ptr::Create(ae::CreateWith{domain});
    model->window = ui::UiDesktopWindow::ptr::Create(ae::CreateWith{domain});
    model->window->BootstrapFrame(100, 100, 640, 480, ui::UiDesktopWindow::InitialPlacement::Explicit);
    BuildWindowPresentationServices(*model->window);
    auto native = ui::windows::WinDesktopWindowPresenter::ptr::Create(ae::CreateWith{domain});
    native->window = model->window; model->window->presenter = native;
    BuildEmptyWindowTitle(*native);
    if (two_windows) {
      model->second_window = ui::UiDesktopWindow::ptr::Create(ae::CreateWith{domain});
      model->second_window->BootstrapFrame(200, 200, 400, 300, ui::UiDesktopWindow::InitialPlacement::Explicit);
      BuildWindowPresentationServices(*model->second_window);
      model->second_window->device_state = model->window->device_state;
      auto second = ui::windows::WinDesktopWindowPresenter::ptr::Create(ae::CreateWith{domain});
      second->window = model->second_window; model->second_window->presenter = second;
      BuildEmptyWindowTitle(*second);
      InitializeRuntimeNode(*model->second_window);
    }
    InitializeRuntimeNode(*model->window); InitializeRuntimeNode(*model);
    ui::windows::EnsureWinProcessDpiAwareness();
    ui::windows::WinUiEnvironment reader{nullptr};
    model->window->device_state->ObserveEnvironment(reader.snapshot(), ui::UiEnvironmentReason::kStartup);
    ByteSink initial; SerializeInitialPublication(*model, initial);
    session = std::make_unique<ApplicationUiSession>(initial.bytes, nullptr, proxy);
    auto& mirror = static_cast<NativeLocalWindowRoot&>(session->root());
    presenter = static_cast<ui::windows::WinDesktopWindowPresenter*>(&*mirror.window->presenter);
    Pump(); Publish();
  }
  void Pump() { while (!pending.empty()) { auto work = std::move(pending.front()); pending.pop_front(); work(domain); } }
  std::vector<std::uint8_t> Capture() { ByteSink out; SerializeStructuralNodePublication(*model, out); return out.bytes; }
  void Apply(std::vector<std::uint8_t> const& bytes) { session->ApplyStructural(bytes); }
  void Publish() { Apply(Capture()); }
  void Drag(int x, int y, int client_width = 640, int client_height = 480) {
    auto hwnd = presenter->hwnd(); SendMessageW(hwnd, WM_ENTERSIZEMOVE, 0, 0);
    int width{}, height{}; ui::windows::ClientSizeToOuter(client_width, client_height, width, height);
    LOCAL_CHECK(SetWindowPos(hwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE));
    SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);
  }
  void CheckNative(int expected_x, int expected_y) {
    int x{}, y{}, w{}, h{}; ui::windows::ReadModelFrameFromHwnd(presenter->hwnd(), x, y, w, h);
    if (std::abs(x - expected_x) > 2 || std::abs(y - expected_y) > 2) std::cerr << "native expected " << expected_x << "," << expected_y << " actual " << x << "," << y << "\n";
    LOCAL_CHECK(std::abs(x - expected_x) <= 2 && std::abs(y - expected_y) <= 2);
  }
  void CheckNativeSize(int expected_width, int expected_height) {
    int x{}, y{}, w{}, h{}; ui::windows::ReadModelFrameFromHwnd(presenter->hwnd(), x, y, w, h);
    LOCAL_CHECK(std::abs(w - expected_width) <= 3 && std::abs(h - expected_height) <= 3);
  }
  void ColdRestore() {
    Pump();
    auto const root_id = model.id();
    ae::DomainGraph save{&domain};
    auto const saved = save.SaveRoot(model.cached(), root_id);
    LOCAL_CHECK(saved.IsOk());
    session.reset();
    ae::Domain cold{storage};
    auto restored = LoadApplication<NativeLocalWindowRoot>(cold, root_id);
    restored->window->presenter->title->MaterializeLanguage(
        restored->window->presenter->locale->language_index());
    ByteSink initial; SerializeInitialPublication(*restored, initial);
    session = std::make_unique<ApplicationUiSession>(initial.bytes, nullptr, proxy);
    auto& mirror = static_cast<NativeLocalWindowRoot&>(session->root());
    presenter = static_cast<ui::windows::WinDesktopWindowPresenter*>(&*mirror.window->presenter);
  }
  void CheckPortableInitial() {
    LOCAL_CHECK(model->window->x() == 100 && model->window->y() == 100);
    LOCAL_CHECK(model->window->restore_client_width() == 640 && model->window->restore_client_height() == 480);
  }
};
}
