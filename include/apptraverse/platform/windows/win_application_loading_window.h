#ifndef APPTRAVERSE_PLATFORM_WINDOWS_WIN_APPLICATION_LOADING_WINDOW_H_
#define APPTRAVERSE_PLATFORM_WINDOWS_WIN_APPLICATION_LOADING_WINDOW_H_

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

namespace apptraverse {
namespace windows {

class WinApplicationLoadingWindow {
 public:
  WinApplicationLoadingWindow(wchar_t const* class_name, wchar_t const* title,
                              wchar_t const* message, int x, int y, int width,
                              int height);
  ~WinApplicationLoadingWindow();

  WinApplicationLoadingWindow(WinApplicationLoadingWindow const&) = delete;
  WinApplicationLoadingWindow& operator=(WinApplicationLoadingWindow const&) =
      delete;

  void Close();

  [[nodiscard]] bool is_open() const;
  [[nodiscard]] HWND hwnd() const;

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam);
  LRESULT Handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

  HINSTANCE instance_{nullptr};
  HWND hwnd_{nullptr};
  std::wstring class_name_;
  std::wstring paint_text_;
};

}  // namespace windows
}  // namespace apptraverse

#endif  // APPTRAVERSE_PLATFORM_WINDOWS_WIN_APPLICATION_LOADING_WINDOW_H_
