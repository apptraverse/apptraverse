#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#undef RegisterClass
#endif
#include <filesystem>
#include <string_view>
#include <unordered_map>
#include "apptraverse/platform/windows/win_application_host.h"
#include "apptraverse/platform/windows/win_ui_environment.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "surface_ui_session.h"
#include "surface_ui_native_presenter.h"
namespace demo = apptraverse::example::surface_ui;
namespace {
class SurfaceNativeWindow : public demo::SurfacePresenterMixin<apptraverse::ui::windows::WinDesktopWindowPresenter> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surface_ui::NativeWindow", SurfaceNativeWindow, apptraverse::ui::windows::WinDesktopWindowPresenter, 0)
 protected: SurfaceNativeWindow() = default;
 public:
  explicit SurfaceNativeWindow(ae::ObjProp p) : SurfacePresenterMixin{p} {}
  template <class D> void Load(ae::Version<0>, D& d) { d(base_); }
  template <class D> void Save(ae::Version<0>, D& d) const { d(base_); }
};
APPTRAVERSE_REGISTER(SurfaceNativeWindow);
demo::SurfaceFactories Factories() {
  return {[](ae::Domain& d) { return SurfaceNativeWindow::ptr::Create(ae::CreateWith{d}); },
          [](ae::Domain& d) { return apptraverse::ui::windows::WinPushButtonPresenter::ptr::Create(ae::CreateWith{d}); }};
}
std::unordered_map<HWND, ae::ObjId> WindowMap(demo::SurfaceApplication& app) {
  std::unordered_map<HWND, ae::ObjId> result;
  for (auto const& w : app.windows) {
    auto& p = static_cast<apptraverse::ui::windows::WinDesktopWindowPresenter&>(*w->presenter);
    result[p.hwnd()] = w.id();
  }
  return result;
}
std::vector<std::uint32_t> CaptureOrder(demo::SurfaceApplication& app) {
  auto map = WindowMap(app);
  std::vector<std::uint32_t> order;
  struct State { decltype(map)* map; decltype(order)* order; } state{&map, &order};
  EnumWindows([](HWND hwnd, LPARAM arg) -> BOOL {
    auto& s = *reinterpret_cast<State*>(arg);
    if (auto i = s.map->find(hwnd); i != s.map->end()) s.order->push_back(i->second.id());
    return TRUE;
  }, reinterpret_cast<LPARAM>(&state));
  return order;
}
void RestoreOrder(demo::SurfaceApplication& app) {
  auto map = WindowMap(app);
  for (auto it = app.order.rbegin(); it != app.order.rend(); ++it) {
    for (auto const& [hwnd, id] : map) if (id == *it)
      SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
  for (auto const& [hwnd, id] : map) if (!app.order.empty() && id == app.order.front()) SetForegroundWindow(hwnd);
}
}
int main(int argc, char** argv) {
  apptraverse::EnsureCoreObjectRegistration();
  apptraverse::ui::windows::EnsureWinControlPresenterRegistration();
  demo::EnsureSurfaceUiRegistration();
  demo::SurfaceSession session;
  session.factories = Factories();
#if defined(APPTRAVERSE_SURFACE_UI_V1)
  session.state_dir = "surface_generic_v1_s0_state";
#endif
  for (int i = 1; i < argc; ++i) {
    auto arg = std::string_view{argv[i]};
    if (arg == "--state-dir" && i + 1 < argc) session.state_dir = argv[++i];
    else if (arg == "--local-seed-dir" && i + 1 < argc) session.local_seed_dir = argv[++i];
    else if (arg == "--load-only") session.load_only = true;
#if defined(APPTRAVERSE_SURFACE_UI_DEBUG_BOOTSTRAP)
    else if (arg == "--distill-only" && i + 1 < argc) return demo::ExportSurfaceSupply(argv[++i], Factories(),
#if defined(APPTRAVERSE_SURFACE_UI_V1)
      true
#else
      false
#endif
    ) ? 0 : 1;
#endif
    else return 2;
  }
#if defined(APPTRAVERSE_SURFACE_UI_DEBUG_BOOTSTRAP)
  if (!session.load_only && !session.local_seed_dir.empty()) return 2;
#endif
  FreeConsole();
  apptraverse::ui::windows::EnsureWinProcessDpiAwareness();
  apptraverse::ui::windows::WinUiEnvironment environment{nullptr};
  session.environment = environment.snapshot();
  int result = 1;
  apptraverse::windows::WinApplicationHostCallbacks callbacks;
  callbacks.run_model = [&](auto published) { result = session.Run(std::move(published)); };
  callbacks.request_stop = [&] { session.runtime.RequestStop(); };
  callbacks.on_desktop_window_close = [&](ae::Obj& root, ae::Obj&, apptraverse::ModelObjectProxy& proxy) {
    auto& app = static_cast<demo::SurfaceApplication&>(root);
    proxy.Invoke(app.obj_id, &demo::SurfaceApplication::SetOrder, CaptureOrder(app));
    session.runtime.RequestStop(); // Native X exits; content Close removes one.
  };
  callbacks.after_initial = [](ae::Obj& root) { RestoreOrder(static_cast<demo::SurfaceApplication&>(root)); };
  ae::ObjId pending_focus;
  callbacks.take_incremental_sidecar = [&] {
    pending_focus = session.pending_focus_for_ui;
    session.pending_focus_for_ui = {};
  };
  callbacks.after_incremental = [&](ae::Obj& root) {
    auto& app = static_cast<demo::SurfaceApplication&>(root);
    if (pending_focus.is_valid()) {
      for (auto const& [hwnd, id] : WindowMap(app)) if (id == pending_focus) SetForegroundWindow(hwnd);
      pending_focus = {};
    }
  };
  apptraverse::windows::WinApplicationHost host{session.runtime,
      {L"AppTraverseSurfaceUiLoading", L"Loading", L"Loading", 120, 120, 320, 120}, std::move(callbacks)};
  host.Run();
  return result;
}
