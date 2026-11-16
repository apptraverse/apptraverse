#ifndef APPTRAVERSE_SURFACES_WIN_APP_H_
#define APPTRAVERSE_SURFACES_WIN_APP_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <filesystem>
#include <memory>
#include <optional>
#include <thread>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/model_object_proxy.h"

#include "surfaces_lifecycle.h"
#include "surfaces_session_automation.h"
#include "apptraverse/surfaces/surfaces_model.h"

namespace apptraverse {

class WinApp {
 public:
  void SetSessionAutomation(
      std::vector<SurfacesSessionAutomationStep> steps);
  int Run(std::filesystem::path const& state_dir);

#if defined(APPTRAVERSE_SURFACES_WIN32_SMOKE_TEST)
  bool PostModelWork(ModelObjectProxy::ModelWork work);
#endif

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam);
  LRESULT Handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
  void OnInitialPublished();
  void OnIncrementalPublished();
  void QueueAllWindowBounds();
  void RestoreDesktopSurfaceZOrder();
  void QueueDesktopZOrderSnapshot();
  void QueueForegroundAsCurrent();
  void RequestApplicationStop();
  void RunNextSessionAutomationStep();
  void ContinueSessionAutomationAfterPublication();
  Surface* FindUiSurface1();

  SurfacesModelSession session_;
  std::vector<SurfacesSessionAutomationStep> session_automation_;
  std::size_t session_automation_index_{0};
  bool session_automation_waiting_publication_{false};
  bool session_automation_failed_{false};
  std::optional<ModelObjectProxy> model_proxy_;
  std::thread model_thread_;
  HWND notify_{nullptr};
  HWND loading_{nullptr};
  ae::RamDomainStorage ui_storage_;
  std::unique_ptr<ae::Domain> ui_domain_;
  Application::ptr ui_application_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_WIN_APP_H_
