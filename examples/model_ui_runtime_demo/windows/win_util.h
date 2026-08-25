#ifndef APPTRAVERSE_WIN_UTIL_H_
#define APPTRAVERSE_WIN_UTIL_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <string>

#include "model_executor.h"

namespace apptraverse {

inline constexpr UINT WM_APPTRAVERSE_PUBLISHED = WM_APP + 1;

inline std::wstring Utf8ToWide(std::string const& text) {
  if (text.empty()) {
    return {};
  }
  int const n = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                    static_cast<int>(text.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      out.data(), n);
  return out;
}

inline std::string WideToUtf8(std::wstring const& text) {
  if (text.empty()) {
    return {};
  }
  int const n = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                    static_cast<int>(text.size()), nullptr, 0,
                                    nullptr, nullptr);
  std::string out(static_cast<std::size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      out.data(), n, nullptr, nullptr);
  return out;
}

inline std::int32_t WindowDpi(HWND hwnd) {
  HDC hdc = GetDC(hwnd);
  if (hdc == nullptr) {
    return 96;
  }
  int const dpi = GetDeviceCaps(hdc, LOGPIXELSX);
  ReleaseDC(hwnd, hdc);
  return dpi > 0 ? dpi : 96;
}

inline WindowBoundsCommand MakeBoundsCommand(HWND hwnd, std::uint32_t window_id) {
  RECT outer{};
  GetWindowRect(hwnd, &outer);
  RECT client{};
  GetClientRect(hwnd, &client);
  WindowBoundsCommand command;
  command.window_id = window_id;
  command.left = outer.left;
  command.top = outer.top;
  command.right = outer.right;
  command.bottom = outer.bottom;
  command.dpi = WindowDpi(hwnd);
  command.client_width = client.right - client.left;
  command.client_height = client.bottom - client.top;
  return command;
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_WIN_UTIL_H_
