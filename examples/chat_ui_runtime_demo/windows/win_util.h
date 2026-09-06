#ifndef APPTRAVERSE_CHAT_WIN_UTIL_H_
#define APPTRAVERSE_CHAT_WIN_UTIL_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <cstring>

#include <string>

namespace chat::win32 {

inline constexpr UINT WM_APPTRAVERSE_PUBLISHED = WM_APP + 1;
inline constexpr UINT WM_APPTRAVERSE_CONNECTION_UI = WM_APP + 2;
inline constexpr UINT WM_APPTRAVERSE_RUNTIME_DIAG = WM_APP + 3;

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

inline std::string WideToUtf8(std::wstring const& wide) {
  if (wide.empty()) {
    return {};
  }
  int const bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                        static_cast<int>(wide.size()), nullptr,
                                        0, nullptr, nullptr);
  std::string out(static_cast<std::size_t>(bytes), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                      out.data(), bytes, nullptr, nullptr);
  return out;
}

inline std::string ReadEditTextUtf8(HWND edit_hwnd) {
  if (edit_hwnd == nullptr) {
    return {};
  }
  int const n = GetWindowTextLengthW(edit_hwnd);
  std::wstring wide(static_cast<std::size_t>(n), L'\0');
  if (n > 0) {
    GetWindowTextW(edit_hwnd, wide.data(), n + 1);
  }
  return WideToUtf8(wide);
}

inline bool CopyWideTextToClipboard(HWND owner, std::wstring const& text) {
  if (text.empty() || !OpenClipboard(owner)) {
    return false;
  }
  EmptyClipboard();
  std::size_t const bytes = (text.size() + 1) * sizeof(wchar_t);
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (mem == nullptr) {
    CloseClipboard();
    return false;
  }
  void* locked = GlobalLock(mem);
  if (locked == nullptr) {
    GlobalFree(mem);
    CloseClipboard();
    return false;
  }
  std::memcpy(locked, text.data(), text.size() * sizeof(wchar_t));
  static_cast<wchar_t*>(locked)[text.size()] = L'\0';
  GlobalUnlock(mem);
  SetClipboardData(CF_UNICODETEXT, mem);
  CloseClipboard();
  return true;
}

}  // namespace chat::win32

#endif  // APPTRAVERSE_CHAT_WIN_UTIL_H_
