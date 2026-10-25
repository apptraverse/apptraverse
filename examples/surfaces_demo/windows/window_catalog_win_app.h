#ifndef APPTRAVERSE_WINDOW_CATALOG_WIN_APP_H_
#define APPTRAVERSE_WINDOW_CATALOG_WIN_APP_H_

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#undef RegisterClass
#endif

#include <filesystem>
#include <optional>

#include "aether-objects/obj/obj.h"

#include "apptraverse/application_ui_session.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/platform/windows/win_presentation_host.h"

#include "window_catalog_lifecycle.h"
#include "window_catalog_model.h"

namespace apptraverse {

class WindowCatalogWinApp {
 public:
  int Run(std::filesystem::path const& state_dir);

 private:
  void OnInitialPublished();
  void OnIncrementalPublished();
  void RequestApplicationStop();
  void ApplyPendingFocusWindow(ae::ObjId focus_window_id);
  bool IsMainCatalogWindow(ae::Obj const& desktop_window) const;

  WindowCatalog& UiCatalog();
  WindowCatalog const& UiCatalog() const;

  static void OnDesktopWindowUserClose(void* opaque, ae::Obj* desktop_window);

  WindowCatalogModelSession session_;
  std::optional<ModelObjectProxy> model_proxy_;
  std::optional<ApplicationUiSession> ui_session_;
  ui::windows::WinPresentationHost presentation_host_{};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_WIN_APP_H_
