#include "surfaces_demo_win_add_caption.h"

#include "apptraverse/platform/windows/surfaces_win_presenters.h"
#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"
#include "apptraverse/platform/windows/win_ui_utf8.h"
#include "apptraverse/surfaces/surfaces_model.h"

namespace apptraverse {

void SyncWinSurfaceLibraryAddButtonCaptions(Application const& application) {
  if (!application.label_add || !application.locale || !application.surfaces) {
    return;
  }
  int language_index = 0;
  if (auto locale = application.locale.Load()) {
    language_index = locale->language_index();
  }
  std::string caption;
  for (auto const& surface : application.surfaces->surfaces) {
    if (auto add = ui::windows::WinPushButtonPresenter::ptr{
            surface->chrome.add_button}) {
      if (auto button = add->button.Load()) {
        if (auto label = button->label.Load()) {
          caption = label->Resolve(language_index);
          break;
        }
      }
    }
  }
  if (caption.empty()) {
    if (auto label = application.label_add.Load()) {
      caption = label->Resolve(language_index);
    }
  }
  std::wstring wide;
  if (!caption.empty()) {
    wide = ui::windows::Utf8ToWide(caption);
  } else if (language_index == 0) {
    wide = L"Add";
  } else {
    wide = ui::windows::Utf8ToWide("Добавить");
  }
  for (auto const& surface : application.surfaces->surfaces) {
    auto window = Win32SurfaceWindowPresenter::ptr{surface->chrome.window};
    if (!window || window->hwnd == nullptr) {
      continue;
    }
    HWND const btn =
        GetDlgItem(window->hwnd, static_cast<int>(kSurfaceAddButtonId));
    if (btn != nullptr) {
      SetWindowTextW(btn, wide.c_str());
    }
  }
}

}  // namespace apptraverse
