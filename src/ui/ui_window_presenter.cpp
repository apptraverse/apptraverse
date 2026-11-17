#include "apptraverse/ui/ui_window_presenter.h"

#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_presented_window.h"
#include <algorithm>

namespace apptraverse::ui {

bool UiWindowPresenter::PresentsChildControl(Presenter const& child) const {
  if (!NativeCaptionProvidesClose()) return true;
  auto const& presented = static_cast<UiPresentedWindow const&>(*window);
  return child.obj_id != presented.caption_close_control.id();
}
UiLayoutNode const* UiWindowPresenter::LayoutRoot() const {
  auto const& presented = static_cast<UiPresentedWindow const&>(*window);
  auto const& root = presented.layout_state->EffectiveRoot();
  if (!NativeCaptionProvidesClose() || presented.caption_close_control_id == 0) return &root;
  presented_layout_ = root;
  auto filter = [&](auto&& self, UiLayoutNode& node) -> void {
    std::erase_if(node.children, [&](auto const& child) {
      return child.flow == UiLayoutFlow::kLeaf && child.control_id == presented.caption_close_control_id;
    });
    for (auto& child : node.children) self(self, child);
  };
  filter(filter, presented_layout_);
  return &presented_layout_;
}
UiDeviceState* UiWindowPresenter::LocalDeviceState() const {
  return &*static_cast<UiPresentedWindow const&>(*window).device_state;
}

std::string UiWindowPresenter::ResolvePresentedWindowTitle(
    int language_index) const {
  title.Load();
  if (language_index < 0) {
    language_index = locale->language_index();
  }
  return title->Resolve(language_index);
}

}  // namespace apptraverse::ui
