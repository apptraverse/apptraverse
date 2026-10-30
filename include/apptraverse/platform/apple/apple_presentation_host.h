#ifndef APPTRAVERSE_PLATFORM_APPLE_PRESENTATION_HOST_H_
#define APPTRAVERSE_PLATFORM_APPLE_PRESENTATION_HOST_H_

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace ae {
class Obj;
}

namespace apptraverse::ui {
class UiDesktopWindowPresenter;
class UiListContainerPresenter;
class UiPushButtonPresenter;
}  // namespace apptraverse::ui

namespace apptraverse::ui::apple {

// Runtime-only. Passed as Presenter::presentation_host. Not serialized.
struct ApplePresentationHost {
  void* projection{nullptr};
  void* swift_model{nullptr};
  void (*on_desktop_window_user_close)(void* opaque, ae::Obj* window){nullptr};
  void* opaque{nullptr};
  bool report_desktop_geometry{false};
  std::function<void(std::function<void(int status)>)> request_persist;
  std::unordered_map<std::uint64_t, ::apptraverse::ui::UiDesktopWindowPresenter*>
      windows_by_id;
  std::unordered_map<std::uint32_t, ::apptraverse::ui::UiPushButtonPresenter*>
      buttons_by_control;
  ::apptraverse::ui::UiListContainerPresenter* list{nullptr};
  std::uint64_t revision{0};
};

ApplePresentationHost* AppleRequireHost(void* presentation_host);

void AppleBindPresentationHost(ApplePresentationHost* host);
void AppleUnbindPresentationHost(void* projection);
void AppleProjectionSetSessionLabel(void* projection, char const* label);
void AppleProjectionBumpRevision(void* projection);

void AppleSyncDesktopPresenter(
    ::apptraverse::ui::UiDesktopWindowPresenter& presenter);
void AppleSyncListPresenter(
    ::apptraverse::ui::UiListContainerPresenter& presenter);
void AppleSyncButtonPresenter(
    ::apptraverse::ui::UiPushButtonPresenter& presenter);
void AppleRemoveDesktopPresenter(
    ::apptraverse::ui::UiDesktopWindowPresenter& presenter);

void AppleCloseDesktopPresenter(
    ApplePresentationHost* host,
    ::apptraverse::ui::UiDesktopWindowPresenter& presenter);

}  // namespace apptraverse::ui::apple

#endif  // APPTRAVERSE_PLATFORM_APPLE_PRESENTATION_HOST_H_
