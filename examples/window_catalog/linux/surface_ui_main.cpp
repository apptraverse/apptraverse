#include <filesystem>
#include <string_view>
#include "apptraverse/platform/linux/linux_application_host.h"
#include "apptraverse/platform/linux/linux_ui_registration.h"
#include "apptraverse/platform/linux/linux_ui_window_presenter.h"
#include "apptraverse/platform/linux/linux_ui_push_button_presenter.h"
#include "surface_ui_session.h"
#include "surface_ui_native_presenter.h"
namespace demo = apptraverse::example::surface_ui;
namespace {
class SurfaceNativeWindow : public demo::SurfacePresenterMixin<apptraverse::ui::linux::LinuxDesktopWindowPresenter> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surface_ui::NativeWindow", SurfaceNativeWindow, apptraverse::ui::linux::LinuxDesktopWindowPresenter, 0)
 protected: SurfaceNativeWindow() = default;
 public:
  explicit SurfaceNativeWindow(ae::ObjProp p) : SurfacePresenterMixin{p} {}
  template <class D> void Load(ae::Version<0>, D& d) { d(base_); }
  template <class D> void Save(ae::Version<0>, D& d) const { d(base_); }
};
APPTRAVERSE_REGISTER(SurfaceNativeWindow);
demo::SurfaceFactories Factories() {
  return {[](ae::Domain& d) { return SurfaceNativeWindow::ptr::Create(ae::CreateWith{d}); },
    [](ae::Domain& d) { return apptraverse::ui::linux::LinuxPushButtonPresenter::ptr::Create(ae::CreateWith{d}); }};
}
GtkWidget* NativeWindow(demo::SurfaceWindow& w) {
  return static_cast<apptraverse::ui::linux::LinuxDesktopWindowPresenter&>(*w.presenter).TopLevelWindow();
}
std::vector<std::uint32_t> CaptureOrder(demo::SurfaceApplication& app) {
  auto* screen = gdk_screen_get_default();
  auto* stack = gdk_screen_get_window_stack(screen);
  std::vector<std::uint32_t> result;
  for (auto* node = g_list_last(stack); node; node = node->prev) {
    for (auto const& w : app.windows)
      if (gtk_widget_get_window(NativeWindow(*w)) == node->data) result.push_back(w.id().id());
  }
  g_list_free_full(stack, g_object_unref);
  return result;
}
void RestoreOrder(demo::SurfaceApplication& app) {
  for (auto it = app.order.rbegin(); it != app.order.rend(); ++it)
    for (auto const& w : app.windows) if (w.id() == *it) gdk_window_raise(gtk_widget_get_window(NativeWindow(*w)));
  for (auto const& w : app.windows) if (!app.order.empty() && w.id() == app.order.front()) gtk_window_present(GTK_WINDOW(NativeWindow(*w)));
}
}
int main(int argc, char** argv) {
  apptraverse::EnsureCoreObjectRegistration();
  apptraverse::ui::linux::EnsureLinuxUiPresenterRegistration();
  demo::EnsureSurfaceUiRegistration();
  demo::SurfaceSession session;
  session.factories = Factories();
  for (int i = 1; i < argc; ++i) {
    auto arg = std::string_view{argv[i]};
    if (arg == "--state-dir" && i + 1 < argc) session.state_dir = argv[++i];
    else if (arg == "--local-seed-dir" && i + 1 < argc) session.local_seed_dir = argv[++i];
    else if (arg == "--load-only") session.load_only = true;
#if defined(APPTRAVERSE_SURFACE_UI_DEBUG_BOOTSTRAP)
    else if (arg == "--distill-only" && i + 1 < argc) return demo::ExportSurfaceSupply(argv[++i], Factories(), false) ? 0 : 1;
#endif
    else return 2;
  }
#if defined(APPTRAVERSE_SURFACE_UI_DEBUG_BOOTSTRAP)
  if (!session.load_only && !session.local_seed_dir.empty()) return 2;
#endif
  gtk_init(nullptr, nullptr);
  session.environment = apptraverse::ui::linux::ReadLinuxDeviceEnvironment();
  int result = 1;
  apptraverse::linux::LinuxApplicationHostCallbacks callbacks;
  callbacks.run_model = [&](auto published) { result = session.Run(std::move(published)); };
  callbacks.request_stop = [&] { session.runtime.RequestStop(); };
  callbacks.on_desktop_window_close = [&](ae::Obj& root, ae::Obj&, apptraverse::ModelObjectProxy& proxy) {
    auto& app = static_cast<demo::SurfaceApplication&>(root);
    proxy.Invoke(app.obj_id, &demo::SurfaceApplication::SetOrder, CaptureOrder(app));
    session.runtime.RequestStop();
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
      for (auto const& w : app.windows) if (w.id() == pending_focus) gtk_window_present(GTK_WINDOW(NativeWindow(*w)));
      pending_focus = {};
    }
  };
  apptraverse::linux::LinuxApplicationHost host{session.runtime, std::move(callbacks)};
  host.Run();
  return result;
}
