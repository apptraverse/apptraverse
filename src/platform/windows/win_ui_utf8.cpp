#include "apptraverse/platform/windows/win_ui_utf8.h"

#include <windows.h>

namespace apptraverse::ui::windows {

std::wstring Utf8ToWide(std::string_view utf8) {
  if (utf8.empty()) {
    return {};
  }
  int const size =
      MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                          static_cast<int>(utf8.size()), nullptr, 0);
  if (size <= 0) {
    return {};
  }
  std::wstring wide(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                      wide.data(), size);
  return wide;
}

std::string WideToUtf8(std::wstring_view wide) {
  if (wide.empty()) {
    return {};
  }
  int const size = WideCharToMultiByte(
      CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
      nullptr, nullptr);
  if (size <= 0) {
    return {};
  }
  std::string utf8(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                      utf8.data(), size, nullptr, nullptr);
  return utf8;
}

}  // namespace apptraverse::ui::windows
