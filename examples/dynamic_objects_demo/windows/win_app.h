#ifndef APPTRAVERSE_DYNAMIC_WIN_APP_H_
#define APPTRAVERSE_DYNAMIC_WIN_APP_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <filesystem>
#include <memory>
#include <thread>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "dynamic_lifecycle.h"
#include "dynamic_model.h"

namespace apptraverse {

class WinApp {
 public:
  int Run(std::filesystem::path const& state_dir);

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam);
  LRESULT Handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
  void OnInitialPublished();
  void OnIncrementalPublished();

  DynamicModelSession session_;
  std::thread model_thread_;
  HWND notify_{nullptr};
  HWND loading_{nullptr};
  ae::RamDomainStorage ui_storage_;
  std::unique_ptr<ae::Domain> ui_domain_;
  Application::ptr ui_application_;
  std::uint64_t add_sequence_{0};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_DYNAMIC_WIN_APP_H_
