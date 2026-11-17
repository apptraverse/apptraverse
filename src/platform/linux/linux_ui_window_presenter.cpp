#include "apptraverse/platform/linux/linux_ui_window_presenter.h"

#include <string>
#include <algorithm>
#include <cmath>

#include "apptraverse/platform/linux/linux_fatal.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/platform/linux/linux_ui_client_mount.h"
#include "apptraverse/platform/linux/linux_presentation_host.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui::linux {

namespace {
std::string MonitorKey(GdkMonitor* monitor) {
  auto* display = gdk_monitor_get_display(monitor);
  auto* screen = gdk_display_get_default_screen(display);
  for (int i = 0; i < gdk_display_get_n_monitors(display); ++i) {
    if (gdk_display_get_monitor(display, i) != monitor) continue;
    gchar* plug = gdk_screen_get_monitor_plug_name(screen, i);
    std::string key = plug ? plug : ("gdk-index:" + std::to_string(i));
    g_free(plug);
    return key;
  }
  FatalLinux("monitor missing from GDK display");
}
UiDisplayRect DisplayRect(GdkRectangle const& r) { return {r.x, r.y, r.width, r.height}; }
}

UiDeviceEnvironment ReadLinuxDeviceEnvironment() {
  UiDeviceEnvironment snapshot;
  auto* settings = gtk_settings_get_default();
  gchar* theme = nullptr;
  gchar* font = nullptr;
  gboolean dark = FALSE, animations = TRUE;
  gint dpi = -1;
  g_object_get(settings, "gtk-theme-name", &theme, "gtk-font-name", &font,
      "gtk-application-prefer-dark-theme", &dark, "gtk-enable-animations", &animations,
      "gtk-xft-dpi", &dpi, nullptr);
  std::string const theme_name = theme ? theme : "";
  snapshot.supported_settings = kSystemTextScale | kSystemDarkMode | kSystemHighContrast | kSystemReducedMotion;
  snapshot.dark = dark || theme_name.find("dark") != std::string::npos || theme_name.find("Dark") != std::string::npos;
  snapshot.high_contrast = theme_name.find("HighContrast") != std::string::npos;
  snapshot.reduced_motion = !animations;
  snapshot.text_scale_milli = dpi > 0 ? static_cast<unsigned>(std::lround(dpi * 1000.0 / (1024 * 96))) : 1000;
  PangoFontDescription* description = pango_font_description_from_string(font ? font : "Sans 10");
  snapshot.message_font.family = pango_font_description_get_family(description);
  snapshot.message_font.height = pango_font_description_get_size(description) / PANGO_SCALE;
  snapshot.message_font.weight = pango_font_description_get_weight(description);
  snapshot.message_font.italic = pango_font_description_get_style(description) != PANGO_STYLE_NORMAL;
  pango_font_description_free(description);
  g_free(theme); g_free(font);
  GdkDisplay* display = gdk_display_get_default();
  GdkMonitor* primary = gdk_display_get_primary_monitor(display);
  snapshot.system_dpi_x = snapshot.system_dpi_y = primary ? 96 * gdk_monitor_get_scale_factor(primary) : 96;
  auto* style = gtk_style_context_new();
  auto* path = gtk_widget_path_new();
  gtk_widget_path_append_type(path, GTK_TYPE_WINDOW);
  gtk_style_context_set_path(style, path);
  auto color = [&](GdkRGBA c) {
    auto channel = [](double v) { return static_cast<unsigned>(std::lround(std::clamp(v, 0.0, 1.0) * 255)); };
    return channel(c.red) | (channel(c.green) << 8) | (channel(c.blue) << 16);
  };
  GdkRGBA fg{}, bg{};
  gtk_style_context_get_color(style, GTK_STATE_FLAG_NORMAL, &fg);
  gtk_style_context_get_background_color(style, GTK_STATE_FLAG_NORMAL, &bg);
  snapshot.colors = {color(bg), color(fg)};
  gtk_widget_path_unref(path); g_object_unref(style);
  for (int i = 0; i < gdk_display_get_n_monitors(display); ++i) {
    auto* monitor = gdk_display_get_monitor(display, i);
    GdkRectangle bounds{}, work{};
    gdk_monitor_get_geometry(monitor, &bounds); gdk_monitor_get_workarea(monitor, &work);
    int const scale = gdk_monitor_get_scale_factor(monitor);
    bounds = {bounds.x * scale, bounds.y * scale, bounds.width * scale, bounds.height * scale};
    work = {work.x * scale, work.y * scale, work.width * scale, work.height * scale};
    snapshot.monitors.push_back({MonitorKey(monitor), DisplayRect(bounds), DisplayRect(work),
        monitor == primary || (!primary && i == 0), bounds.width >= bounds.height ? 0u : 90u,
        static_cast<unsigned>(std::max(0, gdk_monitor_get_refresh_rate(monitor)) / 1000)});
  }
  NormalizeUiDeviceEnvironment(snapshot);
  return snapshot;
}

void LinuxDesktopWindowPresenter::RegisterChildControl(GtkWidget* widget, std::uint32_t id) {
  native_children_.push_back({widget, id});
  RefreshPresentationSettings();
  RefreshLayout();
}
void LinuxDesktopWindowPresenter::UnregisterChildControl(GtkWidget* widget) {
  std::erase_if(native_children_, [widget](auto const& child) { return child.widget == widget; });
}
bool LinuxDesktopWindowPresenter::NativeMeasurementsReady() const {
  auto* root = LayoutRoot();
  if (!root) return true;
  auto ready = [&](auto&& self, UiLayoutNode const& node) -> bool {
    if (node.flow == UiLayoutFlow::kLeaf) return std::any_of(native_children_.begin(), native_children_.end(),
        [&](auto const& child) { return child.control_id == node.control_id; });
    return std::all_of(node.children.begin(), node.children.end(), [&](auto const& child) { return self(self, child); });
  };
  return ready(ready, *root);
}
UiConstraintLayoutResult LinuxDesktopWindowPresenter::ComputeLocalLayout() const {
  std::vector<UiLayoutMeasurement> measurements;
  for (auto const& child : native_children_) {
    if (!GTK_IS_BUTTON(child.widget)) {
      auto* text = gtk_widget_create_pango_layout(child.widget, GTK_IS_LABEL(child.widget)?gtk_label_get_text(GTK_LABEL(child.widget)):"Mg"); int w{}, h{};
      pango_layout_get_pixel_size(text, &w, &h);
      int minimum=0;if(GTK_IS_LABEL(child.widget)){pango_layout_set_width(text,1);pango_layout_set_wrap(text,PANGO_WRAP_WORD);pango_layout_get_pixel_size(text,&minimum,nullptr);}
      g_object_unref(text);
      measurements.push_back({child.control_id, static_cast<double>(minimum), static_cast<double>(h + 8), GTK_IS_LABEL(child.widget)?static_cast<double>(w):0, static_cast<double>(h+8)});
      continue;
    }
    auto* text = gtk_widget_create_pango_layout(child.widget, gtk_button_get_label(GTK_BUTTON(child.widget)));
    int width{}, height{}; pango_layout_get_pixel_size(text, &width, &height);
    GtkBorder padding{}, border{};
    auto* style = gtk_widget_get_style_context(child.widget);
    gtk_style_context_get_padding(style, GTK_STATE_FLAG_NORMAL, &padding);
    gtk_style_context_get_border(style, GTK_STATE_FLAG_NORMAL, &border);
    double const horizontal =
        padding.left + padding.right + border.left + border.right + 8;
    double const vertical =
        padding.top + padding.bottom + border.top + border.bottom + 4;
    // Pango's word-wrapped minimum keeps every word readable; the engine can
    // overflow instead of shrinking the native font to fit a narrow viewport.
    pango_layout_set_width(text, 1);
    pango_layout_set_wrap(text, PANGO_WRAP_WORD);
    int minimum{};
    pango_layout_get_pixel_size(text, &minimum, nullptr);
    g_object_unref(text);
    measurements.push_back({child.control_id, minimum + horizontal,
                            height + vertical, width + horizontal,
                            height + vertical});
  }
  GtkAllocation viewport{};
  gtk_widget_get_allocation(scroller_, &viewport);
  auto remeasure = [&](std::uint32_t id, double width) {
    auto child =
        std::find_if(native_children_.begin(), native_children_.end(),
                     [&](auto const& c) { return c.control_id == id; });
    auto measurement =
        *std::find_if(measurements.begin(), measurements.end(),
                      [&](auto const& m) { return m.control_id == id; });
    if (GTK_IS_BUTTON(child->widget) || GTK_IS_LABEL(child->widget)) {
      bool const button = GTK_IS_BUTTON(child->widget);
      auto* text = gtk_widget_create_pango_layout(
          child->widget, button
                             ? gtk_button_get_label(GTK_BUTTON(child->widget))
                             : gtk_label_get_text(GTK_LABEL(child->widget)));
      pango_layout_set_width(
          text, static_cast<int>(std::max(1.0, width - (button ? 24 : 0)) *
                                 PANGO_SCALE));
      pango_layout_set_wrap(text, PANGO_WRAP_WORD);
      int height{};
      pango_layout_get_pixel_size(text, nullptr, &height);
      g_object_unref(text);
      measurement.min_height =
          std::max(measurement.min_height,
                   static_cast<double>(height + (button ? 16 : 8)));
      measurement.preferred_height =
          std::max(measurement.preferred_height, measurement.min_height);
    }
    return measurement;
  };
  auto result = ComputeUiConstraintLayout(
      *LayoutRoot(), viewport.width, viewport.height, measurements, remeasure);
  if (!result.valid) FatalLinux(result.error.c_str());
  return result;
}
UiControlLayout LinuxDesktopWindowPresenter::ResolveChildLayout(
    UiControlLayout requested) const {
  if (!LayoutRoot() || !NativeMeasurementsReady()) return requested;
  auto const result = ComputeLocalLayout();
  auto found = std::find_if(
      result.controls.begin(), result.controls.end(),
      [&](auto const& r) { return r.control_id == requested.control_id; });
  if (found == result.controls.end())
    FatalLinux("required control absent from GTK layout");
  return {static_cast<int>(std::lround(found->x)),
          static_cast<int>(std::lround(found->y)),
          std::max(1, static_cast<int>(std::lround(found->width))),
          std::max(1, static_cast<int>(std::lround(found->height))),
          requested.control_id};
}
gboolean LinuxDesktopWindowPresenter::OnLayoutIdle(gpointer data) {
  auto* self = static_cast<LinuxDesktopWindowPresenter*>(data);
  self->layout_idle_ = 0;
  if (!self->NativeMeasurementsReady())
    FatalLinux("GTK layout missing a presented control");
  self->RefreshLayout();
  return G_SOURCE_REMOVE;
}
void LinuxDesktopWindowPresenter::RefreshLayout() {
  if (!window_ || applying_layout_ || !LayoutRoot()) return;
  if (!NativeMeasurementsReady()) {
    if (!layout_idle_) layout_idle_ = g_idle_add(OnLayoutIdle, this);
    return;
  }
  applying_layout_ = true;
  auto const result = ComputeLocalLayout();
  gtk_widget_set_size_request(
      client_mount_, static_cast<int>(std::ceil(result.content_width)),
      static_cast<int>(std::ceil(result.content_height)));
  for (auto const& child : native_children_) {
    if (!gtk_widget_get_visible(child.widget)) continue;
    auto const rect = ResolveChildLayout({0, 0, 1, 1, child.control_id});
    gtk_widget_set_size_request(child.widget, rect.width, rect.height);
    gtk_fixed_move(GTK_FIXED(client_mount_), child.widget, rect.x, rect.y);
  }
  applying_layout_ = false;
}
void LinuxDesktopWindowPresenter::OnSettingsChanged(GObject*, GParamSpec*,
                                                    gpointer data) {
  auto* self = static_cast<LinuxDesktopWindowPresenter*>(data);
  self->ObserveNativeEnvironment(UiEnvironmentReason::kSystemNotification);
  self->RefreshPresentationSettings();
  self->RefreshLayout();
}
void LinuxDesktopWindowPresenter::OnMonitorsChanged(GdkScreen*, gpointer data) {
  auto* self = static_cast<LinuxDesktopWindowPresenter*>(data);
  self->environment_placement_pending_ = true;
  self->ObserveNativeEnvironment(UiEnvironmentReason::kSystemNotification);
}
void LinuxDesktopWindowPresenter::RefreshPresentationSettings() {
  auto* state = LocalDeviceState(); if (!state || !window_) return;
  auto const system = ReadLinuxDeviceEnvironment();
  if (appearance_provider_) {
    auto remove = [&](auto&& self, GtkWidget* widget) -> void {
      gtk_style_context_remove_provider(gtk_widget_get_style_context(widget), GTK_STYLE_PROVIDER(appearance_provider_));
      if (native_theme_provider_)
        gtk_style_context_remove_provider(gtk_widget_get_style_context(widget), GTK_STYLE_PROVIDER(native_theme_provider_));
      if (GTK_IS_CONTAINER(widget)) {
        GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
        for (auto* p = children; p; p = p->next) self(self, GTK_WIDGET(p->data));
        g_list_free(children);
      }
    };
    remove(remove, window_);
    g_object_unref(appearance_provider_);
  }
  if (native_theme_provider_) { g_object_unref(native_theme_provider_); native_theme_provider_ = nullptr; }
  appearance_provider_ = gtk_css_provider_new();
  auto const preference = state->appearance;
  bool const dark = preference.theme == UiThemeMode::kDark || (preference.theme == UiThemeMode::kAuto && system.dark);
  // Native GTK themes retain hover/pressed/disabled/focus styling. High Contrast
  // always uses the system theme, irrespective of the application's preference.
  std::string css;
  if (!system.high_contrast && (preference.theme != UiThemeMode::kAuto || (dark && preference.soften_dark)))
    native_theme_provider_ = GTK_CSS_PROVIDER(g_object_ref(gtk_css_provider_get_named("Adwaita", dark ? "dark" : nullptr)));
  if (!system.high_contrast && dark && preference.soften_dark)
    css += "\nwindow, button, list { background-color: #29323c; color: #e8edf2; } button:disabled { color: #a0a7ae; }";
  gtk_css_provider_load_from_data(appearance_provider_, css.c_str(), -1, nullptr);
  auto apply = [&](auto&& self, GtkWidget* widget) -> void {
    if (native_theme_provider_)
      gtk_style_context_add_provider(gtk_widget_get_style_context(widget), GTK_STYLE_PROVIDER(native_theme_provider_), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_style_context_add_provider(gtk_widget_get_style_context(widget), GTK_STYLE_PROVIDER(appearance_provider_), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    if (GTK_IS_CONTAINER(widget)) {
      GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
      for (auto* p = children; p; p = p->next) self(self, GTK_WIDGET(p->data));
      g_list_free(children);
    }
  };
  apply(apply, window_);
}
void LinuxDesktopWindowPresenter::ObserveNativeEnvironment(UiEnvironmentReason reason) {
  auto* state = LocalDeviceState(); if (!state || !model_proxy || user_close_requested_) return;
  model_proxy->Invoke(state->obj_id, &UiDeviceState::ObserveEnvironment, ReadLinuxDeviceEnvironment(), reason);
}
void LinuxDesktopWindowPresenter::ApplyLocalPlacement() {
  auto* state = LocalDeviceState(); if (!state || !window_ || !mapped_) return;
  auto* placement = state->PlacementFor(window.id());
  if (!placement || placement->revision == placement_revision_ || placement->resolution == UiMonitorResolution::kUnavailable) return;
  // A first placement synthesized from this native window's own observation
  // is already applied. Its publication may arrive after a newer user resize.
  // Seeded placements and later monitor-resolution revisions still apply.
  if (native_initial_placement_observed_ && placement->revision == 1) {
    placement_revision_ = placement->revision;
    native_initial_placement_observed_ = false;
    return;
  }
  auto* native_window = gtk_widget_get_window(window_);
  auto const outer = ReadNativeOuterFrame();
  int const decoration_width = std::max(0, outer.width - gdk_window_get_width(native_window));
  int const decoration_height = std::max(0, outer.height - gdk_window_get_height(native_window));
  auto current = ReadLinuxDeviceEnvironment();
  auto monitor = std::find_if(current.monitors.begin(), current.monitors.end(), [&](auto const& m) { return m.key == placement->resolved_monitor; });
  if (monitor == current.monitors.end()) return; // A queued monitor notification will resolve it through LOCAL.
  auto area = monitor->work_area;
  auto* display = gtk_widget_get_display(window_);
  for (int i = 0; i < gdk_display_get_n_monitors(display); ++i) {
    auto* native_monitor = gdk_display_get_monitor(display, i);
    if (MonitorKey(native_monitor) != monitor->key) continue;
    int const scale = std::max(1, gdk_monitor_get_scale_factor(native_monitor));
    area = {area.x / scale, area.y / scale, area.width / scale, area.height / scale};
    break;
  }
  auto const frame = placement->frame_dip;
  // GDK desktop coordinates are logical pixels (96-DPI DIPs). LOCAL stores
  // the OUTER frame; gtk_window_resize accepts the native CLIENT size.
  int const width = std::clamp(frame.width, decoration_width + 1, std::max(decoration_width + 1, area.width));
  int const height = std::clamp(frame.height, decoration_height + 1, std::max(decoration_height + 1, area.height));
  applying_mirror_frame_ = true;
  gtk_window_resize(GTK_WINDOW(window_), width - decoration_width, height - decoration_height);
  gtk_window_move(GTK_WINDOW(window_), area.x + std::clamp(frame.x, 0, std::max(0, area.width-width)), area.y + std::clamp(frame.y, 0, std::max(0, area.height-height)));
  applying_mirror_frame_ = false;
  frame_reference_ = {area.x + std::clamp(frame.x, 0, std::max(0, area.width-width)),
      area.y + std::clamp(frame.y, 0, std::max(0, area.height-height)), width, height};
  // A no-op request may produce no configure signal at all.
  local_placement_pending_ = ReadNativeOuterFrame() != frame_reference_;
  have_frame_reference_ = true;
  environment_placement_pending_ = false;
  placement_revision_ = placement->revision;
}

GtkWidget* LinuxDesktopWindowPresenter::ClientMount() const {
  return client_mount_;
}

void* LinuxDesktopWindowPresenter::NativeChildParentHandle() const {
  return client_mount_;
}

std::string LinuxDesktopWindowPresenter::ResolveTitleText() const {
  return ResolvePresentedWindowTitle(locale->language_index());
}

bool LinuxDesktopWindowPresenter::FramesEqual(ClientFrame a, ClientFrame b) {
  return a.x == b.x && a.y == b.y && a.client_width == b.client_width &&
         a.client_height == b.client_height;
}

LinuxDesktopWindowPresenter::ClientFrame
LinuxDesktopWindowPresenter::ReadMirrorClientFrame() const {
  auto const desktop = desktop_window();
  return {desktop->x(), desktop->y(), desktop->restore_client_width(),
          desktop->restore_client_height()};
}

LinuxDesktopWindowPresenter::ClientFrame
LinuxDesktopWindowPresenter::ReadNativeClientFrame() const {
  ClientFrame frame;
  // GTK returns the gravity-relative frame position accepted by
  // gtk_window_move. Keep it independent of the requested position, including
  // when the window manager chooses an initial position or moves it offscreen.
  gtk_window_get_position(GTK_WINDOW(window_), &frame.x, &frame.y);
  GtkAllocation allocation;
  gtk_widget_get_allocation(scroller_, &allocation);
  frame.client_width = allocation.width;
  frame.client_height = allocation.height;
  return frame;
}

UiDisplayRect LinuxDesktopWindowPresenter::ReadNativeOuterFrame() const {
  GdkRectangle frame{};
  gdk_window_get_frame_extents(gtk_widget_get_window(window_), &frame);
  return {frame.x, frame.y, frame.width, frame.height};
}

void LinuxDesktopWindowPresenter::HandleNativeFrameChanged() {
  if (applying_mirror_frame_ || user_close_requested_ || !mapped_) {
    return;
  }
  UiDisplayRect const native = ReadNativeOuterFrame();
  if (native.width <= 0 || native.height <= 0) {
    return;
  }
  GdkWindowState const state =
      gdk_window_get_state(gtk_widget_get_window(window_));
  bool const restore =
      (state & (GDK_WINDOW_STATE_MAXIMIZED | GDK_WINDOW_STATE_ICONIFIED |
                GDK_WINDOW_STATE_FULLSCREEN)) == 0;
  auto* device = LocalDeviceState();
  if (device && restore) {
    auto* monitor = gdk_display_get_monitor_at_window(gtk_widget_get_display(window_), gtk_widget_get_window(window_));
    GdkRectangle area{}; gdk_monitor_get_workarea(monitor, &area);
    auto const scale = static_cast<unsigned>(gdk_window_get_scale_factor(gtk_widget_get_window(window_)));
    UiWindowEnvironment observation{window.id(), MonitorKey(monitor), 96u * scale, 96u * scale,
        {native.x - area.x, native.y - area.y, native.width, native.height}};
    bool const reached_placement = local_placement_pending_ && frame_reference_ == native;
    bool const user_change = have_frame_reference_ && frame_reference_ != native &&
        !environment_placement_pending_ && !local_placement_pending_;
    if (!device->PlacementFor(window.id())) native_initial_placement_observed_ = true;
    model_proxy->Invoke(device->obj_id, &UiDeviceState::ObserveWindow,
        observation, user_change ? UiEnvironmentReason::kUserWindowChange : UiEnvironmentReason::kSystemNotification);
    if (reached_placement) local_placement_pending_ = false;
    if (!local_placement_pending_) frame_reference_ = native;
    have_frame_reference_ = true;
  }
  RefreshLayout();
}

void LinuxDesktopWindowPresenter::ReportNativeClientFrame() {
  HandleNativeFrameChanged();
}

void LinuxDesktopWindowPresenter::OnActiveChanged(
    GObject* object, GParamSpec*, gpointer user_data) {
  auto* self = static_cast<LinuxDesktopWindowPresenter*>(user_data);
  if (!self->user_close_requested_ && gtk_window_is_active(GTK_WINDOW(object))) {
    self->NotifyActivatedFromUi();
  }
}

gboolean LinuxDesktopWindowPresenter::OnConfigure(GtkWidget* widget,
                                                  GdkEventConfigure* event,
                                                  gpointer user_data) {
  (void)widget;
  (void)event;
  auto* self = static_cast<LinuxDesktopWindowPresenter*>(user_data);
  self->ApplyLocalPlacement();
  self->HandleNativeFrameChanged();
  return FALSE;
}

gboolean LinuxDesktopWindowPresenter::OnMapEvent(GtkWidget* widget,
                                                GdkEvent* event,
                                                gpointer user_data) {
  (void)event;
  auto* self = static_cast<LinuxDesktopWindowPresenter*>(user_data);
  bool const initial_map = !self->mapped_;
  self->mapped_ = true;
  if (initial_map) self->ApplyLocalPlacement();
  self->HandleNativeFrameChanged();
  return FALSE;
}

void LinuxDesktopWindowPresenter::OnClientMountSizeAllocate(
    GtkWidget* widget, GdkRectangle* allocation, gpointer user_data) {
  (void)widget;
  (void)allocation;
  auto* self = static_cast<LinuxDesktopWindowPresenter*>(user_data);
  self->HandleNativeFrameChanged();
  self->RefreshLayout();
}

gboolean LinuxDesktopWindowPresenter::OnDeleteEvent(GtkWidget* widget,
                                                   GdkEvent* event,
                                                   gpointer user_data) {
  (void)widget;
  (void)event;
  auto* self = static_cast<LinuxDesktopWindowPresenter*>(user_data);
  if (self->user_close_requested_) {
    return TRUE;
  }
  self->HandleNativeFrameChanged();
  // Model Close may unload the target before the publication destroys GTK UI.
  self->user_close_requested_ = true;
  self->DisconnectNativeFrameReports();
  auto* host = static_cast<LinuxPresentationHost*>(self->presentation_host);
  host->on_desktop_window_user_close(host->opaque, self->window.operator->());
  return TRUE;
}

void LinuxDesktopWindowPresenter::ApplyRequestedFrameFromMirror() {
  if (applying_mirror_frame_ || user_close_requested_ || !mapped_) {
    return;
  }
  ClientFrame const mirror = ReadMirrorClientFrame();
  ClientFrame const native = ReadNativeClientFrame();
  if (FramesEqual(mirror, native)) {
    return;
  }
  applying_mirror_frame_ = true;
  gtk_window_resize(GTK_WINDOW(window_), mirror.client_width,
                    mirror.client_height);
  gtk_window_move(GTK_WINDOW(window_), mirror.x, mirror.y);
  applying_mirror_frame_ = false;
}

void LinuxDesktopWindowPresenter::OnLoad() {
  auto const desktop = desktop_window();
  user_close_requested_ = false;
  mapped_ = false;
  placement_revision_ = 0;
  local_placement_pending_ = false;
  native_initial_placement_observed_ = false;
  have_frame_reference_ = false;

  window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  if (window_ == nullptr) {
    FatalLinux("gtk_window_new");
  }
  // Mapping several restored windows must not activate them in construction
  // order. The application presents the model-selected window after mapping.
  gtk_window_set_focus_on_map(GTK_WINDOW(window_), FALSE);
  gtk_widget_add_events(window_, GDK_BUTTON_PRESS_MASK);
  g_signal_connect(window_, "button-press-event", G_CALLBACK((+[](GtkWidget*, GdkEventButton* event, gpointer data) -> gboolean {
    if (event->button != 3) return FALSE;
    auto* self = static_cast<LinuxDesktopWindowPresenter*>(data);
    auto* state = self->LocalDeviceState();
    auto* menu = gtk_menu_new();
    struct Choice { LinuxDesktopWindowPresenter* presenter; UiAppearancePreferences preferences; };
    auto add = [&](char const* text, UiAppearancePreferences preferences, bool active) {
      auto* item = gtk_check_menu_item_new_with_label(text);
      gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), active);
      auto* choice = new Choice{self, preferences};
      g_signal_connect_data(item, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer data) {
        auto* c = static_cast<Choice*>(data);
        c->presenter->model_proxy->Invoke(c->presenter->LocalDeviceState()->obj_id, &UiDeviceState::SetAppearance, c->preferences);
      }), choice, +[](gpointer data, GClosure*) { delete static_cast<Choice*>(data); }, GConnectFlags(0));
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    };
    char const* labels[] = {"Auto", "Light", "Dark"};
    for (int mode = 0; mode < 3; ++mode)
      add(labels[mode], {static_cast<UiThemeMode>(mode), state->appearance.soften_dark}, static_cast<int>(state->appearance.theme) == mode);
    add("Soften dark theme", {state->appearance.theme, !state->appearance.soften_dark}, state->appearance.soften_dark);
    g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event));
    return TRUE;
  })), this);
  g_object_set_data(G_OBJECT(window_), "apptraverse_presenter", this);
  delete_handler_ = g_signal_connect(window_, "delete-event",
                                     G_CALLBACK(OnDeleteEvent), this);
  active_handler_ = g_signal_connect(window_, "notify::is-active",
                                      G_CALLBACK(OnActiveChanged), this);
  map_handler_ =
      g_signal_connect(window_, "map-event", G_CALLBACK(OnMapEvent), this);
  configure_handler_ = g_signal_connect(window_, "configure-event",
                                        G_CALLBACK(OnConfigure), this);

  client_mount_ = CreateClientMount();
  if (client_mount_ == nullptr) {
    FatalLinux("CreateClientMount");
  }
  scroller_ = gtk_scrolled_window_new(nullptr, nullptr);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller_), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(scroller_), client_mount_);
  gtk_container_add(GTK_CONTAINER(window_), scroller_);
  settings_handler_ = g_signal_connect(gtk_settings_get_default(), "notify", G_CALLBACK(OnSettingsChanged), this);
  monitors_handler_ = g_signal_connect(gdk_screen_get_default(), "monitors-changed", G_CALLBACK(OnMonitorsChanged), this);
  size_allocate_handler_ =
      g_signal_connect(client_mount_, "size-allocate",
                       G_CALLBACK(OnClientMountSizeAllocate), this);

  applying_mirror_frame_ = true;
  gtk_window_set_default_size(GTK_WINDOW(window_),
                               desktop->restore_client_width(),
                               desktop->restore_client_height());
  if (desktop->HasExplicitPosition()) {
    gtk_window_move(GTK_WINDOW(window_), desktop->x(), desktop->y());
    gtk_window_set_geometry_hints(GTK_WINDOW(window_), nullptr, nullptr,
                                  GDK_HINT_USER_POS);
  }
  applying_mirror_frame_ = false;
  SyncTitleFromMirror();
  RefreshPresentationSettings();
  ApplyLocalPlacement();
  gtk_widget_show_all(window_);
  RefreshLayout();
}

void LinuxDesktopWindowPresenter::SyncTitleFromMirror() {
  std::string const title_text = ResolveTitleText();
  gtk_window_set_title(GTK_WINDOW(window_), title_text.c_str());
}

void LinuxDesktopWindowPresenter::OnModelChanged() {
  SyncTitleFromMirror();
  RefreshPresentationSettings();
  ApplyLocalPlacement();
  RefreshLayout();
}

void LinuxDesktopWindowPresenter::DisconnectNativeFrameReports() {
  if (configure_handler_ != 0) {
    g_signal_handler_disconnect(window_, configure_handler_);
    configure_handler_ = 0;
  }
  if (size_allocate_handler_ != 0) {
    g_signal_handler_disconnect(client_mount_, size_allocate_handler_);
    size_allocate_handler_ = 0;
  }
}

void LinuxDesktopWindowPresenter::OnUnload() {
  if (layout_idle_) { g_source_remove(layout_idle_); layout_idle_ = 0; }
  g_signal_handler_disconnect(gtk_settings_get_default(), settings_handler_);
  g_signal_handler_disconnect(gdk_screen_get_default(), monitors_handler_);
  settings_handler_ = monitors_handler_ = 0;
  if (appearance_provider_) { g_object_unref(appearance_provider_); appearance_provider_ = nullptr; }
  if (native_theme_provider_) { g_object_unref(native_theme_provider_); native_theme_provider_ = nullptr; }
  native_children_.clear();
  g_signal_handler_disconnect(window_, active_handler_);
  active_handler_ = 0;
  g_signal_handler_disconnect(window_, delete_handler_);
  g_signal_handler_disconnect(window_, map_handler_);
  delete_handler_ = 0;
  map_handler_ = 0;
  DisconnectNativeFrameReports();
  gtk_widget_destroy(window_);
  window_ = nullptr;
  client_mount_ = nullptr;
  mapped_ = false;
}

}  // namespace apptraverse::ui::linux
