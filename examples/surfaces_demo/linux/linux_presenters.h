#ifndef APPTRAVERSE_SURFACES_LINUX_PRESENTERS_H_
#define APPTRAVERSE_SURFACES_LINUX_PRESENTERS_H_

#include <cstdint>

#include <gtk/gtk.h>

#include "apptraverse/object_macros.h"

#include "desktop_surface_presenter.h"

namespace apptraverse {

class LinuxApp;

void EnsureLinuxSurfacePresenterRegistration();

class LinuxSurfacePresenter : public DesktopSurfacePresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::LinuxSurfacePresenter",
      LinuxSurfacePresenter, DesktopSurfacePresenter, 0)

 protected:
  LinuxSurfacePresenter() = default;

 public:
  explicit LinuxSurfacePresenter(ae::ObjProp prop)
      : DesktopSurfacePresenter{prop} {}

  AE_OBJECT_REFLECT()

  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;
  bool OnCommand(std::uint32_t command_id,
                 std::uint16_t notification_code) override;

  void QueueCurrentBounds();

  GtkWidget* window{nullptr};
  GtkWidget* add_button{nullptr};
  GtkWidget* close_button{nullptr};
  LinuxApp* app{nullptr};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_LINUX_PRESENTERS_H_
