#include "apptraverse/platform/windows/win_ui_native_push_button.h"

#include "apptraverse/platform/windows/win32_fatal.h"

namespace apptraverse::ui::windows {
namespace {

void SetChildUserData(HWND hwnd, void* value, char const* fatal_operation) {
  SetLastError(0);
  LONG_PTR const previous =
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(value));
  if (previous == 0 && GetLastError() != 0) {
    FatalWin32(fatal_operation, GetLastError());
  }
}

}  // namespace

HWND CreateChildPushButton(WinNativePushButtonCreateParams const& params) {
  if (params.parent_hwnd == nullptr || params.control_id == 0 ||
      params.create_fatal_operation == nullptr) {
    FatalWin32(params.create_fatal_operation != nullptr
                   ? params.create_fatal_operation
                   : "CreateChildPushButton",
               ERROR_INVALID_PARAMETER);
  }
  HWND const hwnd = CreateWindowExW(
      0, L"BUTTON",
      params.initial_caption != nullptr ? params.initial_caption : L"",
      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, params.x, params.y, params.width,
      params.height, params.parent_hwnd,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(params.control_id)),
      GetModuleHandleW(nullptr), nullptr);
  if (hwnd == nullptr) {
    FatalWin32(params.create_fatal_operation, GetLastError());
  }
  SetChildUserData(hwnd, params.presenter_user_data,
                   "SetWindowLongPtrW GWLP_USERDATA");
  return hwnd;
}

void DestroyChildPushButton(HWND& hwnd, char const* destroy_fatal_operation) {
  if (hwnd == nullptr) {
    return;
  }
  SetChildUserData(hwnd, nullptr, "SetWindowLongPtrW GWLP_USERDATA");
  if (DestroyWindow(hwnd) == 0) {
    FatalWin32(destroy_fatal_operation, GetLastError());
  }
  hwnd = nullptr;
}

}  // namespace apptraverse::ui::windows
