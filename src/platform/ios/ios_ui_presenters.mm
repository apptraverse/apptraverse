#include "apptraverse/platform/ios/ios_ui_presenters.h"

#include "apptraverse/platform/apple/apple_presentation_host.h"

namespace apptraverse::ui::ios {

void IosDesktopWindowPresenter::OnLoad() {
  ui::apple::AppleSyncDesktopPresenter(*this);
}

void IosDesktopWindowPresenter::OnModelChanged() {
  // Mirror title and stored desktop frame only. Viewport size is not a frame.
  ui::apple::AppleSyncDesktopPresenter(*this);
}

void IosDesktopWindowPresenter::OnUnload() {
  ui::apple::AppleRemoveDesktopPresenter(*this);
}

void IosPushButtonPresenter::OnLoad() {
  ui::apple::AppleSyncButtonPresenter(*this);
}

void IosPushButtonPresenter::OnModelChanged() {
  ui::apple::AppleSyncButtonPresenter(*this);
}

void IosPushButtonPresenter::OnUnload() {
  auto* host = ui::apple::AppleRequireHost(presentation_host);
  auto const found = host->buttons_by_control.find(control_id);
  if (found != host->buttons_by_control.end() && found->second == this) {
    host->buttons_by_control.erase(found);
  }
}

void IosListContainerPresenter::OnLoad() {
  ui::apple::AppleSyncListPresenter(*this);
}

void IosListContainerPresenter::OnModelChanged() {
  ui::apple::AppleSyncListPresenter(*this);
}

void IosListContainerPresenter::OnUnload() {
  auto* host = ui::apple::AppleRequireHost(presentation_host);
  if (host->list == this) {
    host->list = nullptr;
  }
}

}  // namespace apptraverse::ui::ios
