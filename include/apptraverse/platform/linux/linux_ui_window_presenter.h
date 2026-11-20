#ifndef APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_WINDOW_PRESENTER_H_
#define APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_WINDOW_PRESENTER_H_

#include <cstdint>
#include <vector>
#include <optional>
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
  GtkWidget* PresentationMenu() const { return presentation_menu_; }
  virtual GtkWidget* CreatePresentationMenu() { return nullptr; }

  static constexpr std::uint32_t kFullscreenActionId = 5300;
  virtual bool HasFullscreenAction() const { return false; }
  virtual UiLayoutNode const* FullscreenActionLayout() const { return nullptr; }
  virtual std::string ResolveFullscreenActionText() const { return {}; }
  virtual void ToggleFullscreenFromUi() {}
  virtual void ExitFullscreenFromUi() {}
  virtual bool ShowPresentationContextMenu(GdkEvent* event) { return false; }
  void SetPresentationFullscreen(bool fullscreen);
  bool PresentationFullscreen() const { return presentation_fullscreen_; }
  GtkWidget* FullscreenAction() const { return fullscreen_action_; }
  int NativeChromeHeight() const;
  void ReportNativeClientFrame();

  void ApplyRequestedFrameFromMirror() override;
  UiControlLayout ResolveChildLayout(UiControlLayout requested) const override;
  void RegisterChildControl(GtkWidget* child, std::uint32_t control_id);
  void UnregisterChildControl(GtkWidget* child);
  void RefreshLayout();
  void RefreshPresentationSettings();
  void ApplyNativeAppearance(GtkWidget* subtree);

 private:
  struct ClientFrame {
    std::int32_t x{0};
    std::int32_t y{0};
    std::int32_t client_width{0};
    std::int32_t client_height{0};
  };

  std::string ResolveTitleText() const;
  void SyncTitleFromMirror();
  ClientFrame ReadMirrorClientFrame() const;
  ClientFrame ReadNativeClientFrame() const;
  UiDisplayRect ReadNativeOuterFrame() const;
  void HandleNativeFrameChanged();
  void ApplyRequestedNativeFrame(ClientFrame frame);
  void DisconnectNativeFrameReports();
  void ObserveNativeEnvironment(UiEnvironmentReason reason);
  void ApplyLocalPlacement();
  void ApplyNativeFullscreen();
  void RefreshFullscreenAction();
  bool NativeFrameMatches(UiDisplayRect const& frame) const;
  bool NativeGlobalPositionAvailable() const;
  void RefreshMonitorSubscriptions();
  static void OnMonitorNotify(GObject*, GParamSpec*, gpointer);
  static void OnMonitorSetChanged(GdkDisplay*, GdkMonitor*, gpointer);
  static gboolean OnWindowState(GtkWidget*, GdkEventWindowState*, gpointer);
  static gboolean OnKeyPress(GtkWidget*, GdkEventKey*, gpointer);
  static gboolean OnContextButton(GtkWidget*, GdkEventButton*, gpointer);
  static gboolean OnContextPopup(GtkWidget*, gpointer);
  static void OnFullscreenClicked(GtkButton*, gpointer);
  UiConstraintLayoutResult ComputeLocalLayout() const;
  bool NativeMeasurementsReady() const;
  static void OnSettingsChanged(GObject*, GParamSpec*, gpointer);
  static void OnMonitorsChanged(GdkScreen*, gpointer);
  static gboolean OnLayoutIdle(gpointer);
  struct NativeChild { GtkWidget* widget; std::uint32_t control_id; };
  std::vector<NativeChild> native_children_;
  GtkWidget* scroller_{nullptr};
  GtkWidget* presentation_menu_{nullptr};
  GtkWidget* fullscreen_toolbar_{nullptr};
  GtkWidget* fullscreen_action_{nullptr};
  bool presentation_fullscreen_{false};
  bool fullscreen_transition_{false};
  bool restoring_fullscreen_{false};
  int normal_decoration_width_{0}, normal_decoration_height_{0};
  std::uint64_t fullscreen_placement_revision_{0};
  struct MonitorSubscription { GdkMonitor* monitor; gulong handler; };
  std::vector<MonitorSubscription> monitor_subscriptions_;
  gulong monitor_added_handler_{0}, monitor_removed_handler_{0};
  std::optional<UiAppearancePreferences> applied_appearance_;
  std::optional<UiDeviceEnvironment> applied_environment_;
  std::optional<UiWindowEnvironment> last_reported_window_;
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
  UiDisplayRect frame_reference_{};
  bool local_placement_pending_{false};
  bool native_initial_placement_observed_{false};
  std::optional<std::uint64_t> pending_environment_revision_;
  std::optional<ClientFrame> normal_client_reference_;
  std::optional<ClientFrame> windowed_client_;
  std::optional<ClientFrame> pending_restore_client_;
  std::uint64_t windowed_placement_revision_{0};
  std::optional<ClientFrame> pending_requested_frame_;
  std::optional<ClientFrame> requested_native_frame_;
};

}  // namespace apptraverse::ui::linux

#endif  // APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_WINDOW_PRESENTER_H_
