#ifndef APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_UTF8_H_
#define APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_UTF8_H_

#include <cstddef>
#include <string>
#include <string_view>

namespace apptraverse::ui::windows {

std::wstring Utf8ToWide(std::string_view utf8);
std::string WideToUtf8(std::wstring_view wide);
std::size_t Utf8CaretFromWideSelection(std::string_view utf8,
                                       std::wstring_view wide, int wide_caret);

}  // namespace apptraverse::ui::windows

#endif  // APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_UTF8_H_
