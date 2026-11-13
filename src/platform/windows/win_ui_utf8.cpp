#include "apptraverse/platform/windows/win_ui_utf8.h"

#include <windows.h>
#include <limits>

#include "apptraverse/platform/windows/win32_fatal.h"

namespace apptraverse::ui::windows {

std::wstring Utf8ToWide(std::string_view utf8) {
  if (utf8.empty()) {
    return {};
  }
  if (utf8.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
    FatalWin32("MultiByteToWideChar input length", ERROR_INVALID_PARAMETER);
  }
  int const size =
      MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                          static_cast<int>(utf8.size()), nullptr, 0);
  if (size <= 0) {
    FatalWin32("MultiByteToWideChar size", GetLastError());
  }
  std::wstring wide(static_cast<std::size_t>(size), L'\0');
  if (MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                         wide.data(), size) != size) {
    FatalWin32("MultiByteToWideChar text", GetLastError());
  }
  return wide;
}

std::string WideToUtf8(std::wstring_view wide) {
  if (wide.empty()) {
    return {};
  }
  if (wide.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
    FatalWin32("WideCharToMultiByte input length", ERROR_INVALID_PARAMETER);
  }
  int const size = WideCharToMultiByte(
      CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
      nullptr, nullptr);
  if (size <= 0) {
    FatalWin32("WideCharToMultiByte size", GetLastError());
  }
  std::string utf8(static_cast<std::size_t>(size), '\0');
  if (WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                         utf8.data(), size, nullptr, nullptr) != size) {
    FatalWin32("WideCharToMultiByte text", GetLastError());
  }
  return utf8;
}

}  // namespace apptraverse::ui::windows
