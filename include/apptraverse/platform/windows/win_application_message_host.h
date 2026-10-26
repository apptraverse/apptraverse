#ifndef APPTRAVERSE_PLATFORM_WINDOWS_WIN_APPLICATION_MESSAGE_HOST_H_
#define APPTRAVERSE_PLATFORM_WINDOWS_WIN_APPLICATION_MESSAGE_HOST_H_

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <functional>
#include <thread>

#include "apptraverse/application_model_runtime.h"

namespace apptraverse {
namespace windows {

class WinApplicationMessageHost {
 public:
  using Callback = std::function<void()>;
  using ModelThreadBody = std::function<void()>;

  WinApplicationMessageHost(Callback on_initial, Callback on_incremental,
                            Callback on_stop);
  ~WinApplicationMessageHost();

  WinApplicationMessageHost(WinApplicationMessageHost const&) = delete;
  WinApplicationMessageHost& operator=(WinApplicationMessageHost const&) =
      delete;

  [[nodiscard]] HWND notify_hwnd() const { return notify_hwnd_; }

  void StartModelThread(ModelThreadBody run_model);
  void NotifyPublished(ApplicationPublicationKind kind);
  void PumpUntilModelDone(Callback on_quit);
  void JoinModelThread();

 private:
  static LRESULT CALLBACK NotifyWndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                        LPARAM lparam);
  LRESULT HandleNotifyMessage(UINT msg, WPARAM wparam, LPARAM lparam);

  HINSTANCE instance_{nullptr};
  HWND notify_hwnd_{nullptr};
  HANDLE done_event_{nullptr};
  std::thread model_thread_;
  Callback on_initial_;
  Callback on_incremental_;
  Callback on_stop_;
};

}  // namespace windows
}  // namespace apptraverse

#endif  // APPTRAVERSE_PLATFORM_WINDOWS_WIN_APPLICATION_MESSAGE_HOST_H_
