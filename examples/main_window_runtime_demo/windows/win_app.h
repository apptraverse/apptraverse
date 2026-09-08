#ifndef APPTRAVERSE_MAIN_WINDOW_WIN_APP_H_
#define APPTRAVERSE_MAIN_WINDOW_WIN_APP_H_

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <thread>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "main_window_lifecycle.h"
#include "main_window_model.h"

namespace apptraverse {

inline wchar_t const kLoadingWindowClass[] = L"AppTraverseExampleLoading";
inline wchar_t const kLoadingWindowTitle[] = L"Loading";
inline wchar_t const kNotifyWindowClass[] = L"AppTraverseExampleNotify";

class WinApp {
 public:
  int Run(std::filesystem::path const& state_dir);
  void SetHoldStage(ModelStartupStage stage);
  std::uint32_t GuiPresenterClassId() const {
    return gui_presenter_class_id_.load(std::memory_order_acquire);
  }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                   LPARAM lparam);
  LRESULT Handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
  void OnPublished();
  void RequestStop();
  void DestroyGuiMirror();

  ModelSession session_;
  std::thread model_thread_;
  HWND loading_{nullptr};
  HWND notify_{nullptr};
  bool accept_input_{true};
  bool stop_requested_{false};
  ae::RamDomainStorage ui_storage_;
  std::unique_ptr<ae::Domain> ui_domain_;
  Application::ptr ui_application_;
  std::atomic<std::uint32_t> gui_presenter_class_id_{0};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_MAIN_WINDOW_WIN_APP_H_
