#ifndef APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_NATIVE_PUSH_BUTTON_H_
#define APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_NATIVE_PUSH_BUTTON_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>

namespace apptraverse::ui::windows {

struct WinNativePushButtonCreateParams {
  HWND parent_hwnd{nullptr};
  std::uint32_t control_id{0};
  wchar_t const* initial_caption{L""};
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{0};
  std::int32_t height{0};
  void* presenter_user_data{nullptr};
  char const* create_fatal_operation{nullptr};
};

// WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON. Binds GWLP_USERDATA on success.
HWND CreateChildPushButton(WinNativePushButtonCreateParams const& params);

// No-op when hwnd is already null (normal OnUnload).
void DestroyChildPushButton(HWND& hwnd, char const* destroy_fatal_operation);

}  // namespace apptraverse::ui::windows

#endif  // APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_NATIVE_PUSH_BUTTON_H_
