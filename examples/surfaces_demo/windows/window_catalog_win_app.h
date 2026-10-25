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
#include <memory>
#include <optional>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/platform/windows/win_presentation_host.h"

#include "window_catalog_lifecycle.h"
#include "window_catalog_model.h"

namespace apptraverse {

class WindowCatalogWinApp {
 public:
  int Run(std::filesystem::path const& state_dir);

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam);
  LRESULT Handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
  void OnInitialPublished();
  void OnIncrementalPublished();
  void RequestApplicationStop();
  void ApplyPendingFocusWindow(ae::ObjId focus_window_id);
  bool IsMainCatalogWindow(ae::Obj const& desktop_window) const;

  static void OnDesktopWindowUserClose(void* opaque, ae::Obj* desktop_window);

  WindowCatalogModelSession session_;
  std::optional<ModelObjectProxy> model_proxy_;
  HWND loading_{nullptr};
  ui::windows::WinPresentationHost presentation_host_{};
  ae::RamDomainStorage ui_storage_;
  std::unique_ptr<ae::Domain> ui_domain_;
  WindowCatalog::ptr ui_catalog_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_WIN_APP_H_
