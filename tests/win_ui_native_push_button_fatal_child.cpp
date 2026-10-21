#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/windows/win_ui_native_push_button.h"

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::ui::windows::CreateChildPushButton({
      .parent_hwnd = nullptr,
      .control_id = 1,
      .create_fatal_operation = "CreateWindowExW native push button fatal test",
  });
  return 0;
}
