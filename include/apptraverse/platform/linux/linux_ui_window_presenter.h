#ifndef APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_WINDOW_PRESENTER_H_
#define APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_WINDOW_PRESENTER_H_

#include <cstdint>

#include <gtk/gtk.h>

#include "apptraverse/ui/ui_native_presenter_schema.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::linux {

class LinuxDesktopWindowPresenter : public UiDesktopWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      ::apptraverse::ui::native_schema::kWindow,
      LinuxDesktopWindowPresenter, UiDesktopWindowPresenter, 0)

 protected:
  LinuxDesktopWindowPresenter() = default;

 public:
  explicit LinuxDesktopWindowPresenter(ae::ObjProp prop)
      : UiDesktopWindowPresenter{prop} {}

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_);
  }

  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;

  void* NativeChildParentHandle() const override;

  GtkWidget* ClientMount() const;
  GtkWidget* TopLevelWindow() const { return window_; }

  void ReportNativeClientFrame();

  void ApplyRequestedFrameFromMirror() override;

 private:
  struct ClientFrame {
    std::int32_t x{0};
    std::int32_t y{0};
    std::int32_t client_width{0};
    std::int32_t client_height{0};
  };

  static bool FramesEqual(ClientFrame a, ClientFrame b);

  std::string ResolveTitleText() const;
  void SyncTitleFromMirror();
  ClientFrame ReadMirrorClientFrame() const;
  ClientFrame ReadNativeClientFrame() const;
  void HandleNativeFrameChanged();
  void DisconnectNativeFrameReports();

  static gboolean OnDeleteEvent(GtkWidget* widget, GdkEvent* event,
                                gpointer user_data);
  static void OnActiveChanged(GObject* object, GParamSpec*, gpointer user_data);
  static gboolean OnConfigure(GtkWidget* widget, GdkEventConfigure* event,
                              gpointer user_data);
  static gboolean OnMapEvent(GtkWidget* widget, GdkEvent* event,
                             gpointer user_data);
  static void OnClientMountSizeAllocate(GtkWidget* widget, GdkRectangle* allocation,
                                        gpointer user_data);

  GtkWidget* window_{nullptr};
  GtkWidget* client_mount_{nullptr};
  gulong active_handler_{0};
  gulong delete_handler_{0};
  gulong map_handler_{0};
  gulong configure_handler_{0};
  gulong size_allocate_handler_{0};
  bool applying_mirror_frame_{false};
  bool user_close_requested_{false};
  bool mapped_{false};
};

}  // namespace apptraverse::ui::linux

#endif  // APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_WINDOW_PRESENTER_H_
