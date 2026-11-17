#ifndef APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_WINDOW_PRESENTER_H_
#define APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_WINDOW_PRESENTER_H_

#include <cstdint>
#include <vector>
#include "apptraverse/ui/ui_constraint_layout.h"
#include "apptraverse/ui/ui_device_state.h"

#include <gtk/gtk.h>

#include "apptraverse/ui/ui_native_presenter_schema.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::linux {

// Called on the GTK thread, before the model's initial publication.
UiDeviceEnvironment ReadLinuxDeviceEnvironment();

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

  bool NativeCaptionProvidesClose() const override { return true; }
  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;

  void* NativeChildParentHandle() const override;

  GtkWidget* ClientMount() const;
  GtkWidget* ClientViewport() const { return scroller_; }
  GtkWidget* TopLevelWindow() const { return window_; }

  void ReportNativeClientFrame();

  void ApplyRequestedFrameFromMirror() override;
  UiControlLayout ResolveChildLayout(UiControlLayout requested) const override;
  void RegisterChildControl(GtkWidget* child, std::uint32_t control_id);
  void UnregisterChildControl(GtkWidget* child);
  void RefreshLayout();
  void RefreshPresentationSettings();

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
  UiDisplayRect ReadNativeOuterFrame() const;
  void HandleNativeFrameChanged();
  void DisconnectNativeFrameReports();
  void ObserveNativeEnvironment(UiEnvironmentReason reason);
  void ApplyLocalPlacement();
  UiConstraintLayoutResult ComputeLocalLayout() const;
  bool NativeMeasurementsReady() const;
  static void OnSettingsChanged(GObject*, GParamSpec*, gpointer);
  static void OnMonitorsChanged(GdkScreen*, gpointer);
  static gboolean OnLayoutIdle(gpointer);
  struct NativeChild { GtkWidget* widget; std::uint32_t control_id; };
  std::vector<NativeChild> native_children_;
  GtkWidget* scroller_{nullptr};
  GtkCssProvider* appearance_provider_{nullptr};
  GtkCssProvider* native_theme_provider_{nullptr};
  gulong settings_handler_{0}, monitors_handler_{0};
  guint layout_idle_{0};
  bool applying_layout_{false};
  std::uint64_t placement_revision_{0};

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
  bool have_frame_reference_{false};
  UiDisplayRect frame_reference_{};
  bool local_placement_pending_{false};
  bool native_initial_placement_observed_{false};
  bool environment_placement_pending_{false};
};

}  // namespace apptraverse::ui::linux

#endif  // APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_WINDOW_PRESENTER_H_
