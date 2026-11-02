#ifndef APPTRAVERSE_PLATFORM_LINUX_LINUX_PRESENTATION_HOST_H_
#define APPTRAVERSE_PLATFORM_LINUX_LINUX_PRESENTATION_HOST_H_

namespace ae {
class Obj;
}

namespace apptraverse::ui::linux {

struct LinuxPresentationHost {
  void (*on_desktop_window_user_close)(void* opaque, ae::Obj* desktop_window){
      nullptr};
  void* opaque{nullptr};
};

}  // namespace apptraverse::ui::linux

#endif  // APPTRAVERSE_PLATFORM_LINUX_LINUX_PRESENTATION_HOST_H_
