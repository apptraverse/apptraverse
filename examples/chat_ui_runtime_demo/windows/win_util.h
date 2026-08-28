#ifndef APPTRAVERSE_CHAT_WIN_UTIL_H_
#define APPTRAVERSE_CHAT_WIN_UTIL_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <string>

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

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_WIN_UTIL_H_
