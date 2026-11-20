#include "apptraverse/platform/linux/linux_ui_window_presenter.h"

#include <string>
#include <algorithm>
#include <cmath>
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif

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
    std::string key = plug ? plug : "";
    g_free(plug);
    if (!key.empty()) return key;
    auto description = [](GdkMonitor* value) {
      auto const* manufacturer = gdk_monitor_get_manufacturer(value);
      auto const* model = gdk_monitor_get_model(value);
      return std::string{"gdk-description:"} + (manufacturer ? manufacturer : "") + ":" + (model ? model : "");
    };
    key = description(monitor);
    unsigned matches = 0;
    for (int index = 0; index < gdk_display_get_n_monitors(display); ++index)
      if (description(gdk_display_get_monitor(display, index)) == key) ++matches;
    // Older compositors may supply no connector name. A unique hardware
    // description survives enumeration changes; identical/unknown monitors
    // require an index fallback and cannot promise stable endpoint identity.
    if (matches > 1) key += ":index:" + std::to_string(i);
    return key;
  }
  return {}; // A monitor object can outlive its removal during hotplug.
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
  ApplyNativeAppearance(widget);
  RefreshLayout();
}
void LinuxDesktopWindowPresenter::UnregisterChildControl(GtkWidget* widget) {
  std::erase_if(native_children_, [widget](auto const& child) { return child.widget == widget; });
}
bool LinuxDesktopWindowPresenter::NativeMeasurementsReady() const {
  auto* root = LayoutRoot();
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
  if (!NativeMeasurementsReady()) return requested;
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
  if (!window_ || applying_layout_) return;
  RefreshFullscreenAction();
  if (!NativeMeasurementsReady()) {
    if (!layout_idle_) layout_idle_ = g_idle_add(OnLayoutIdle, this);
    return;
  }
  applying_layout_ = true;
  auto const result = ComputeLocalLayout();
  SetNativeSizeRequestIfChanged(
      client_mount_, static_cast<int>(std::ceil(result.content_width)),
      static_cast<int>(std::ceil(result.content_height)));
  for (auto const& child : native_children_) {
    if (!gtk_widget_get_visible(child.widget)) continue;
    auto const rect = std::find_if(result.controls.begin(), result.controls.end(),
        [&](auto const& r) { return r.control_id == child.control_id; });
    if (rect == result.controls.end()) FatalLinux("required control absent from GTK layout");
    SetClientMountChildFrameIfChanged(child.widget,
        static_cast<int>(std::lround(rect->x)), static_cast<int>(std::lround(rect->y)),
        std::max(1, static_cast<int>(std::lround(rect->width))),
        std::max(1, static_cast<int>(std::lround(rect->height))));
  }
  applying_layout_ = false;
}
void LinuxDesktopWindowPresenter::OnSettingsChanged(GObject*, GParamSpec*,
                                                    gpointer data) {
  auto* self = static_cast<LinuxDesktopWindowPresenter*>(data);
  self->applied_environment_.reset();
  self->ObserveNativeEnvironment(UiEnvironmentReason::kSystemNotification);
  self->RefreshPresentationSettings();
  self->RefreshLayout();
}
void LinuxDesktopWindowPresenter::OnMonitorsChanged(GdkScreen*, gpointer data) {
  auto* self = static_cast<LinuxDesktopWindowPresenter*>(data);
  auto current = ReadLinuxDeviceEnvironment();
  // Duplicate monitor notifications need no model publication to acknowledge.
  if (self->LocalDeviceState()->environment != current)
    self->pending_environment_revision_ = self->LocalDeviceState()->observation_revision;
  else
    self->pending_environment_revision_.reset();
  if (self->model_proxy && !self->user_close_requested_)
    self->model_proxy->Invoke(self->LocalDeviceState()->obj_id,
        &UiDeviceState::ObserveEnvironment, current, UiEnvironmentReason::kSystemNotification);
}
void LinuxDesktopWindowPresenter::OnMonitorNotify(GObject*, GParamSpec*, gpointer data) {
  OnMonitorsChanged(nullptr, data);
}
void LinuxDesktopWindowPresenter::OnMonitorSetChanged(GdkDisplay*, GdkMonitor*, gpointer data) {
  auto* self = static_cast<LinuxDesktopWindowPresenter*>(data);
  self->RefreshMonitorSubscriptions();
  OnMonitorsChanged(nullptr, data);
}
void LinuxDesktopWindowPresenter::RefreshMonitorSubscriptions() {
  for (auto const& value : monitor_subscriptions_) {
    g_signal_handler_disconnect(value.monitor, value.handler);
    g_object_unref(value.monitor);
  }
  monitor_subscriptions_.clear();
  auto* display = gtk_widget_get_display(window_);
  for (int index = 0; index < gdk_display_get_n_monitors(display); ++index) {
    auto* monitor = gdk_display_get_monitor(display, index);
    g_object_ref(monitor);
    monitor_subscriptions_.push_back({monitor,
        g_signal_connect(monitor, "notify", G_CALLBACK(OnMonitorNotify), this)});
  }
}
bool LinuxDesktopWindowPresenter::NativeGlobalPositionAvailable() const {
#ifdef GDK_WINDOWING_WAYLAND
  if (GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(window_))) return false;
#endif
  return true;
}
bool LinuxDesktopWindowPresenter::NativeFrameMatches(UiDisplayRect const& frame) const {
  auto const native = ReadNativeOuterFrame();
  return native.width == frame.width && native.height == frame.height &&
      (!NativeGlobalPositionAvailable() || (native.x == frame.x && native.y == frame.y));
}
void LinuxDesktopWindowPresenter::RefreshFullscreenAction() {
  if (!fullscreen_action_) return;
  auto const label = ResolveFullscreenActionText();
  if (label != gtk_button_get_label(GTK_BUTTON(fullscreen_action_)))
    gtk_button_set_label(GTK_BUTTON(fullscreen_action_), label.c_str());
  auto* text = gtk_widget_create_pango_layout(fullscreen_action_, label.c_str());
  int width{}, height{}, minimum{};
  pango_layout_get_pixel_size(text, &width, &height);
  pango_layout_set_width(text, 1);
  pango_layout_set_wrap(text, PANGO_WRAP_WORD);
  pango_layout_get_pixel_size(text, &minimum, nullptr);
  GtkBorder padding{}, border{};
  auto* style = gtk_widget_get_style_context(fullscreen_action_);
  gtk_style_context_get_padding(style, GTK_STATE_FLAG_NORMAL, &padding);
  gtk_style_context_get_border(style, GTK_STATE_FLAG_NORMAL, &border);
  int const horizontal = padding.left + padding.right + border.left + border.right + 8;
  int const vertical = padding.top + padding.bottom + border.top + border.bottom + 4;
  auto const measurement = UiLayoutMeasurement{kFullscreenActionId,
      static_cast<double>(minimum + horizontal), static_cast<double>(height + vertical),
      static_cast<double>(width + horizontal), static_cast<double>(height + vertical)};
  auto remeasure = [&](std::uint32_t, double allocated_width) {
    auto result = measurement;
    pango_layout_set_width(text, std::max(1, static_cast<int>(allocated_width - horizontal)) * PANGO_SCALE);
    pango_layout_get_pixel_size(text, nullptr, &height);
    result.min_height = result.preferred_height = height + vertical;
    return result;
  };
  int client_width{}, client_height{};
  gtk_window_get_size(GTK_WINDOW(window_), &client_width, &client_height);
  auto result = ComputeUiConstraintLayout(*FullscreenActionLayout(),
      mapped_ ? client_width : desktop_window()->restore_client_width(), 0, {measurement}, remeasure);
  g_object_unref(text);
  if (!result.valid) FatalLinux(result.error.c_str());
  SetNativeSizeRequestIfChanged(fullscreen_toolbar_, -1, static_cast<int>(std::ceil(result.content_height)));
  auto const& rect = result.controls.front();
  SetClientMountChildFrameIfChanged(fullscreen_action_,
      static_cast<int>(std::lround(rect.x)), static_cast<int>(std::lround(rect.y)),
      std::max(1, static_cast<int>(std::lround(rect.width))),
      std::max(1, static_cast<int>(std::lround(rect.height))));
}
void LinuxDesktopWindowPresenter::SetPresentationFullscreen(bool fullscreen) {
  if (presentation_fullscreen_ != fullscreen) {
    if (fullscreen && mapped_ && !(gdk_window_get_state(gtk_widget_get_window(window_)) & GDK_WINDOW_STATE_FULLSCREEN)) {
      windowed_client_ = ReadNativeClientFrame();
      auto const* placement = LocalDeviceState()->PlacementFor(window.id());
      windowed_placement_revision_ = placement ? placement->revision : 0;
    }
    presentation_fullscreen_ = fullscreen;
    automatic_monitor_target_.reset();
    fullscreen_transition_ = true;
    fullscreen_placement_revision_ = 0;
    if (!fullscreen) { placement_revision_ = 0; restoring_fullscreen_ = true; }
    if (mapped_ && ((gdk_window_get_state(gtk_widget_get_window(window_)) & GDK_WINDOW_STATE_FULLSCREEN) != 0) == fullscreen)
      fullscreen_transition_ = false;
  }
  ApplyNativeFullscreen();
  if (mapped_ && !presentation_fullscreen_ && !fullscreen_transition_ && pending_requested_frame_) {
    auto const requested = *pending_requested_frame_;
    pending_requested_frame_.reset();
    ApplyRequestedNativeFrame(requested);
  }
  RefreshFullscreenAction();
}
void LinuxDesktopWindowPresenter::ApplyNativeFullscreen() {
  if (!window_ || !mapped_) return;
  auto* placement = LocalDeviceState()->PlacementFor(window.id());
  if (!presentation_fullscreen_) {
    if (fullscreen_transition_) gtk_window_unfullscreen(GTK_WINDOW(window_));
    return;
  }
  auto const revision = placement ? placement->revision : 0;
  if (!fullscreen_transition_ && fullscreen_placement_revision_ == revision) return;
  auto* display = gtk_widget_get_display(window_);
  int target = -1;
  if (placement) {
    for (int index = 0; index < gdk_display_get_n_monitors(display); ++index)
      if (MonitorKey(gdk_display_get_monitor(display, index)) == placement->resolved_monitor) target = index;
  }
  if (target >= 0) {
    automatic_monitor_target_ = placement->resolved_monitor;
    gtk_window_fullscreen_on_monitor(GTK_WINDOW(window_), gdk_display_get_default_screen(display), target);
  } else {
    gtk_window_fullscreen(GTK_WINDOW(window_));
  }
  fullscreen_placement_revision_ = revision;
}
gboolean LinuxDesktopWindowPresenter::OnWindowState(GtkWidget*, GdkEventWindowState* event, gpointer data) {
  auto* self = static_cast<LinuxDesktopWindowPresenter*>(data);
  if (!(event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN)) return FALSE;
  bool const fullscreen = (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) != 0;
  if (fullscreen == self->presentation_fullscreen_) {
    self->fullscreen_transition_ = false;
    if (!fullscreen) {
      self->ApplyLocalPlacement();
      if (self->pending_requested_frame_) {
        auto const requested = *self->pending_requested_frame_;
        self->pending_requested_frame_.reset();
        self->ApplyRequestedNativeFrame(requested);
      }
      self->HandleNativeFrameChanged();
    }
  } else {
    // The published model remains the display authority when the compositor
    // changes native mode. Reassert it, including a settled external exit.
    self->fullscreen_transition_ = true;
    self->fullscreen_placement_revision_ = 0;
    self->ApplyNativeFullscreen();
  }
  self->RefreshLayout();
  return FALSE;
}
gboolean LinuxDesktopWindowPresenter::OnKeyPress(GtkWidget*, GdkEventKey* event, gpointer data) {
  auto* self = static_cast<LinuxDesktopWindowPresenter*>(data);
  if (!self->HasFullscreenAction() || self->user_close_requested_) return FALSE;
  if (event->keyval == GDK_KEY_F11) { self->ToggleFullscreenFromUi(); return TRUE; }
  if (event->keyval == GDK_KEY_Escape && self->presentation_fullscreen_) {
    self->ExitFullscreenFromUi(); return TRUE;
  }
  return FALSE;
}
void LinuxDesktopWindowPresenter::OnFullscreenClicked(GtkButton*, gpointer data) {
  auto* self = static_cast<LinuxDesktopWindowPresenter*>(data);
  if (!self->user_close_requested_) self->ToggleFullscreenFromUi();
}
gboolean LinuxDesktopWindowPresenter::OnContextButton(GtkWidget*, GdkEventButton* event, gpointer data) {
  if (event->type != GDK_BUTTON_PRESS || event->button != 3) return FALSE;
  return static_cast<LinuxDesktopWindowPresenter*>(data)->ShowPresentationContextMenu(reinterpret_cast<GdkEvent*>(event));
}
gboolean LinuxDesktopWindowPresenter::OnContextPopup(GtkWidget*, gpointer data) {
  return static_cast<LinuxDesktopWindowPresenter*>(data)->ShowPresentationContextMenu(nullptr);
}
void LinuxDesktopWindowPresenter::RefreshPresentationSettings() {
  if (!window_) return;
  auto* state = LocalDeviceState();
  auto const system = ReadLinuxDeviceEnvironment();
  if (applied_appearance_ == state->appearance && applied_environment_ == system) return;
  applied_appearance_ = state->appearance;
  applied_environment_ = system;
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
      // GtkMenuItem's attached submenu is not an ordinary container child.
      if (GTK_IS_MENU_ITEM(widget)) {
        auto* submenu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(widget));
        if (submenu) self(self, submenu);
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
    css += "\nwindow, button, list, menu, menubar { background-color: #29323c; color: #e8edf2; } button:disabled { color: #a0a7ae; }";
  gtk_css_provider_load_from_data(appearance_provider_, css.c_str(), -1, nullptr);
  ApplyNativeAppearance(window_);
}
void LinuxDesktopWindowPresenter::ApplyNativeAppearance(GtkWidget* subtree) {
  auto apply = [&](auto&& self, GtkWidget* widget) -> void {
    if (native_theme_provider_)
      gtk_style_context_add_provider(gtk_widget_get_style_context(widget), GTK_STYLE_PROVIDER(native_theme_provider_), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    if (appearance_provider_)
      gtk_style_context_add_provider(gtk_widget_get_style_context(widget), GTK_STYLE_PROVIDER(appearance_provider_), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    if (GTK_IS_CONTAINER(widget)) {
      GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
      for (auto* p = children; p; p = p->next) self(self, GTK_WIDGET(p->data));
      g_list_free(children);
    }
    if (GTK_IS_MENU_ITEM(widget)) {
      auto* submenu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(widget));
      if (submenu) self(self, submenu);
    }
  };
  apply(apply, subtree);
}
void LinuxDesktopWindowPresenter::ObserveNativeEnvironment(UiEnvironmentReason reason) {
  auto* state = LocalDeviceState(); if (!model_proxy || user_close_requested_) return;
  model_proxy->Invoke(state->obj_id, &UiDeviceState::ObserveEnvironment, ReadLinuxDeviceEnvironment(), reason);
}
void LinuxDesktopWindowPresenter::ApplyLocalPlacement() {
  auto* state = LocalDeviceState(); if (!window_ || !mapped_) return;
  // An accepted environment publication can keep this placement revision, or
  // coalesce this observation with a newer settings snapshot. Its environment
  // revision acknowledges both cases without waiting for exact snapshot data.
  if (pending_environment_revision_ && state->observation_revision != *pending_environment_revision_)
    pending_environment_revision_.reset();
  if (presentation_fullscreen_ || fullscreen_transition_ ||
      (gdk_window_get_state(gtk_widget_get_window(window_)) & GDK_WINDOW_STATE_FULLSCREEN)) return;
  auto* placement = state->PlacementFor(window.id());
  if (!placement || placement->revision == placement_revision_ || placement->resolution == UiMonitorResolution::kUnavailable) return;
  // Only a backend that can move normal windows has an output acknowledgement
  // to await here. Wayland chooses normal-window output itself.
  if (NativeGlobalPositionAvailable()) automatic_monitor_target_ = placement->resolved_monitor;
  // A first placement synthesized from this native window's own observation
  // is already applied. Its publication may arrive after a newer user resize.
  // Seeded placements and later monitor-resolution revisions still apply.
  if (native_initial_placement_observed_ && placement->revision == 1) {
    placement_revision_ = placement->revision;
    native_initial_placement_observed_ = false;
    return;
  }
  auto const outer = ReadNativeOuterFrame();
  int client_width{}, client_height{};
  gtk_window_get_size(GTK_WINDOW(window_), &client_width, &client_height);
  // GTK resize dimensions exclude both X11 server decorations and Wayland
  // client-side decorations; GdkWindow dimensions include the latter.
  int const decoration_width = restoring_fullscreen_ ? normal_decoration_width_ : std::max(0, outer.width - client_width);
  int const decoration_height = restoring_fullscreen_ ? normal_decoration_height_ : std::max(0, outer.height - client_height);
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
  if (restoring_fullscreen_ && windowed_client_ && placement->revision == windowed_placement_revision_) {
    // Wayland CSD/shadow extents can animate without changing the normal
    // content. Restore that native client when topology has not changed.
    pending_restore_client_ = windowed_client_;
    gtk_window_resize(GTK_WINDOW(window_), windowed_client_->client_width,
        windowed_client_->client_height + NativeChromeHeight());
  } else {
    pending_restore_client_.reset();
    gtk_window_resize(GTK_WINDOW(window_), width - decoration_width, height - decoration_height);
  }
  if (NativeGlobalPositionAvailable())
    gtk_window_move(GTK_WINDOW(window_), area.x + std::clamp(frame.x, 0, std::max(0, area.width-width)), area.y + std::clamp(frame.y, 0, std::max(0, area.height-height)));
  applying_mirror_frame_ = false;
  frame_reference_ = {area.x + std::clamp(frame.x, 0, std::max(0, area.width-width)),
      area.y + std::clamp(frame.y, 0, std::max(0, area.height-height)), width, height};
  // A no-op request may produce no configure signal at all.
  auto const native_client = ReadNativeClientFrame();
  local_placement_pending_ = pending_restore_client_
      ? native_client.client_width != pending_restore_client_->client_width || native_client.client_height != pending_restore_client_->client_height
      : !NativeFrameMatches(frame_reference_);
  if (!local_placement_pending_) { restoring_fullscreen_ = false; pending_restore_client_.reset(); }
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
  if (NativeGlobalPositionAvailable())
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
  auto* device = LocalDeviceState();
  auto const* placement = device->PlacementFor(window.id());
  bool const restore =
      (!presentation_fullscreen_ || !placement) &&
      (!fullscreen_transition_ || (presentation_fullscreen_ && !placement)) &&
      (state & (GDK_WINDOW_STATE_MAXIMIZED | GDK_WINDOW_STATE_ICONIFIED |
                GDK_WINDOW_STATE_FULLSCREEN)) == 0;
  if (restore || (!fullscreen_transition_ && presentation_fullscreen_ && placement)) {
    // Configure/size signals can precede the accepted topology publication.
    // Re-enumerate before attributing a relocation to the user.
    auto const current = ReadLinuxDeviceEnvironment();
    if (native_monitor_reference_ && std::none_of(current.monitors.begin(), current.monitors.end(),
        [&](auto const& monitor) { return monitor.key == *native_monitor_reference_; })) {
      // The first valid native sample after its output disappears is automatic
      // fallback, including a compositor that reports the relocation late.
      native_monitor_reference_.reset();
      normal_client_reference_.reset();
    }
    if (current.monitors != device->environment.monitors) {
      OnMonitorsChanged(nullptr, this);
      RefreshLayout();
      return;
    }
    if (restore && restoring_fullscreen_ && local_placement_pending_ && !pending_restore_client_) {
      int client_width{}, client_height{};
      gtk_window_get_size(GTK_WINDOW(window_), &client_width, &client_height);
      int const decoration_width = std::max(0, native.width - client_width);
      int const decoration_height = std::max(0, native.height - client_height);
      if ((decoration_width > 0 || decoration_height > 0) &&
          (decoration_width != normal_decoration_width_ || decoration_height != normal_decoration_height_)) {
        normal_decoration_width_ = decoration_width;
        normal_decoration_height_ = decoration_height;
        placement_revision_ = 0;
        ApplyLocalPlacement();
        return;
      }
    }
    auto* monitor = gdk_display_get_monitor_at_window(gtk_widget_get_display(window_), gtk_widget_get_window(window_));
    if (!monitor) return; // No display is temporarily available during hotplug.
    GdkRectangle area{}; gdk_monitor_get_workarea(monitor, &area);
    auto const scale = static_cast<unsigned>(gdk_window_get_scale_factor(gtk_widget_get_window(window_)));
    auto const key = MonitorKey(monitor);
    if (key.empty()) return;
    UiWindowEnvironment observation{window.id(), key, 96u * scale, 96u * scale,
        {native.x - area.x, native.y - area.y, native.width, native.height}};
    if (!NativeGlobalPositionAvailable()) {
      // Wayland does not publish global window positions. Retain the authored
      // monitor-relative position while observing the compositor's real size.
      observation.frame_dip.x = placement ? placement->frame_dip.x : 0;
      observation.frame_dip.y = placement ? placement->frame_dip.y : 0;
    }
    auto const client = ReadNativeClientFrame();
    bool const reached_placement = local_placement_pending_ &&
        (pending_restore_client_ ? client.client_width == pending_restore_client_->client_width && client.client_height == pending_restore_client_->client_height
                                 : NativeFrameMatches(frame_reference_));
    bool const requested_reached = requested_native_frame_ &&
        client.client_width == requested_native_frame_->client_width && client.client_height == requested_native_frame_->client_height &&
        (!NativeGlobalPositionAvailable() || (client.x == requested_native_frame_->x && client.y == requested_native_frame_->y));
    bool const native_user_delta = normal_client_reference_ &&
        (NativeGlobalPositionAvailable() ? frame_reference_ != native :
            normal_client_reference_->client_width != client.client_width ||
            normal_client_reference_->client_height != client.client_height);
    // Wayland can move a window between same-scale outputs without changing
    // its client size or publishing global coordinates. Compare actual native
    // observations, never actual output against the saved preference: the
    // compositor is allowed to choose a different output at startup.
    bool const automatic_monitor = automatic_monitor_target_ && *automatic_monitor_target_ == key;
    bool const automatic_relocation = automatic_monitor && native_monitor_reference_ && *native_monitor_reference_ != key;
    bool const monitor_delta = native_monitor_reference_ && *native_monitor_reference_ != key && !automatic_monitor;
    // A pending Wayland resize cannot account for an output change: normal
    // windows have no native position request there. Keep that independent
    // user intent even while the compositor has not acknowledged the size.
    bool const monitor_user_change = monitor_delta &&
        (!restore || !NativeGlobalPositionAvailable() || !local_placement_pending_);
    bool const user_change = !pending_environment_revision_ &&
        (monitor_user_change || (restore &&
            (requested_reached || (!local_placement_pending_ && !automatic_relocation && native_user_delta))));
    if (user_change && placement &&
        (!restore || (local_placement_pending_ && !reached_placement && !requested_reached)))
      // A monitor-only observation must retain the normal restore frame while
      // fullscreen or while a normal resize still has transient native bounds.
      observation.frame_dip = placement->frame_dip;
    if (restore && !placement) native_initial_placement_observed_ = true;
    if (!last_reported_window_ || *last_reported_window_ != observation || user_change) {
      model_proxy->Invoke(device->obj_id, &UiDeviceState::ObserveWindow,
          observation, user_change ? UiEnvironmentReason::kUserWindowChange : UiEnvironmentReason::kSystemNotification);
      last_reported_window_ = observation;
    }
    native_monitor_reference_ = key;
    // A topology/fullscreen request may reach its output after the topology
    // publication and size acknowledgement. Consume that delayed relocation as
    // system input, preserving a missing preferred monitor across fallback.
    if (automatic_monitor || user_change) automatic_monitor_target_.reset();
    if (restore) {
      if (requested_reached || user_change) requested_native_frame_.reset();
      if (reached_placement) { local_placement_pending_ = false; restoring_fullscreen_ = false; pending_restore_client_.reset(); }
      if (!restoring_fullscreen_) {
        int client_width{}, client_height{};
        gtk_window_get_size(GTK_WINDOW(window_), &client_width, &client_height);
        normal_decoration_width_ = std::max(0, native.width - client_width);
        normal_decoration_height_ = std::max(0, native.height - client_height);
      }
      if (!local_placement_pending_) { frame_reference_ = native; normal_client_reference_ = client; }
    }
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
  if (initial_map) {
    self->ApplyNativeFullscreen();
    if (!self->presentation_fullscreen_ && !self->fullscreen_transition_ && self->pending_requested_frame_) {
      auto const requested = *self->pending_requested_frame_;
      self->pending_requested_frame_.reset();
      self->ApplyRequestedNativeFrame(requested);
    }
  }
  return FALSE;
}

void LinuxDesktopWindowPresenter::OnViewportSizeAllocate(
    GtkWidget*, GtkAllocation* allocation, gpointer user_data) {
  auto* self = static_cast<LinuxDesktopWindowPresenter*>(user_data);
  if (self->viewport_width_ == allocation->width &&
      self->viewport_height_ == allocation->height) return;
  self->viewport_width_ = allocation->width;
  self->viewport_height_ = allocation->height;
  // Measuring or resizing children during GTK allocation can invalidate the
  // same allocation and schedule another frame at the same viewport size.
  if (!self->viewport_resize_idle_)
    self->viewport_resize_idle_ = g_idle_add(OnViewportResizeIdle, self);
}

gboolean LinuxDesktopWindowPresenter::OnViewportResizeIdle(gpointer user_data) {
  auto* self = static_cast<LinuxDesktopWindowPresenter*>(user_data);
  self->viewport_resize_idle_ = 0;
  self->HandleNativeFrameChanged();
  return G_SOURCE_REMOVE;
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
  if (applying_mirror_frame_ || user_close_requested_) return;
  auto const frame = ReadMirrorClientFrame();
  if (!mapped_ || presentation_fullscreen_ || fullscreen_transition_) {
    pending_requested_frame_ = frame;
    return;
  }
  ApplyRequestedNativeFrame(frame);
}
void LinuxDesktopWindowPresenter::ApplyRequestedNativeFrame(ClientFrame frame) {
  requested_native_frame_ = frame;
  local_placement_pending_ = false;
  restoring_fullscreen_ = false;
  pending_restore_client_.reset();
  if (auto const* placement = LocalDeviceState()->PlacementFor(window.id())) placement_revision_ = placement->revision;
  applying_mirror_frame_ = true;
  gtk_window_resize(GTK_WINDOW(window_), frame.client_width,
                    frame.client_height + NativeChromeHeight());
  if (NativeGlobalPositionAvailable()) gtk_window_move(GTK_WINDOW(window_), frame.x, frame.y);
  applying_mirror_frame_ = false;
  // Equal-data requests still express placement intent and may emit no signal.
  HandleNativeFrameChanged();
}

int LinuxDesktopWindowPresenter::NativeChromeHeight() const {
  int height = 0, minimum{}, natural{};
  if (presentation_menu_) {
    gtk_widget_get_preferred_height(presentation_menu_, &minimum, &natural);
    height += natural;
  }
  if (fullscreen_toolbar_) {
    gtk_widget_get_preferred_height(fullscreen_toolbar_, &minimum, &natural);
    height += natural;
  }
  return height;
}

void LinuxDesktopWindowPresenter::OnLoad() {
  auto const desktop = desktop_window();
  user_close_requested_ = false;
  mapped_ = false;
  viewport_width_ = viewport_height_ = 0;
  placement_revision_ = 0;
  local_placement_pending_ = false;
  native_initial_placement_observed_ = false;
  pending_environment_revision_.reset();
  applied_appearance_.reset();
  applied_environment_.reset();
  last_reported_window_.reset();
  pending_requested_frame_.reset();
  requested_native_frame_.reset();
  normal_client_reference_.reset();
  native_monitor_reference_.reset();
  automatic_monitor_target_.reset();
  windowed_client_.reset();
  pending_restore_client_.reset();
  windowed_placement_revision_ = 0;

  presentation_fullscreen_ = false;
  fullscreen_transition_ = false;
  fullscreen_placement_revision_ = 0;
  restoring_fullscreen_ = false;
  normal_decoration_width_ = normal_decoration_height_ = 0;
  window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  if (window_ == nullptr) {
    FatalLinux("gtk_window_new");
  }
  // Mapping several restored windows must not activate them in construction
  // order. The application presents the model-selected window after mapping.
  gtk_window_set_focus_on_map(GTK_WINDOW(window_), FALSE);
  presentation_menu_ = CreatePresentationMenu();
  g_object_set_data(G_OBJECT(window_), "apptraverse_presenter", this);
  delete_handler_ = g_signal_connect(window_, "delete-event",
                                     G_CALLBACK(OnDeleteEvent), this);
  active_handler_ = g_signal_connect(window_, "notify::is-active",
                                      G_CALLBACK(OnActiveChanged), this);
  map_handler_ =
      g_signal_connect(window_, "map-event", G_CALLBACK(OnMapEvent), this);
  configure_handler_ = g_signal_connect(window_, "configure-event",
                                        G_CALLBACK(OnConfigure), this);

  g_signal_connect(window_, "window-state-event", G_CALLBACK(OnWindowState), this);
  g_signal_connect(window_, "key-press-event", G_CALLBACK(OnKeyPress), this);
  g_signal_connect(window_, "popup-menu", G_CALLBACK(OnContextPopup), this);
  client_mount_ = CreateClientMount();
  if (client_mount_ == nullptr) {
    FatalLinux("CreateClientMount");
  }
  scroller_ = gtk_scrolled_window_new(nullptr, nullptr);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller_), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(scroller_), client_mount_);
  if (presentation_menu_ || HasFullscreenAction()) {
    auto* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    if (presentation_menu_)
      gtk_box_pack_start(GTK_BOX(content), presentation_menu_, FALSE, FALSE, 0);
    if (HasFullscreenAction()) {
      fullscreen_toolbar_ = CreateClientMount();
      fullscreen_action_ = gtk_button_new_with_label(ResolveFullscreenActionText().c_str());
      gtk_label_set_line_wrap(GTK_LABEL(gtk_bin_get_child(GTK_BIN(fullscreen_action_))), TRUE);
      gtk_container_add(GTK_CONTAINER(fullscreen_toolbar_), fullscreen_action_);
      gtk_box_pack_start(GTK_BOX(content), fullscreen_toolbar_, FALSE, FALSE, 0);
      g_signal_connect(fullscreen_action_, "clicked", G_CALLBACK(OnFullscreenClicked), this);
    }
    gtk_box_pack_start(GTK_BOX(content), scroller_, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(window_), content);
  } else {
    gtk_container_add(GTK_CONTAINER(window_), scroller_);
  }
  gtk_widget_add_events(client_mount_, GDK_BUTTON_PRESS_MASK);
  gtk_widget_add_events(scroller_, GDK_BUTTON_PRESS_MASK);
  g_signal_connect(client_mount_, "button-press-event", G_CALLBACK(OnContextButton), this);
  g_signal_connect(scroller_, "button-press-event", G_CALLBACK(OnContextButton), this);
  auto* display = gtk_widget_get_display(window_);
  monitor_added_handler_ = g_signal_connect(display, "monitor-added", G_CALLBACK(OnMonitorSetChanged), this);
  monitor_removed_handler_ = g_signal_connect(display, "monitor-removed", G_CALLBACK(OnMonitorSetChanged), this);
  RefreshMonitorSubscriptions();
  settings_handler_ = g_signal_connect(gtk_settings_get_default(), "notify", G_CALLBACK(OnSettingsChanged), this);
  monitors_handler_ = g_signal_connect(gdk_screen_get_default(), "monitors-changed", G_CALLBACK(OnMonitorsChanged), this);
  size_allocate_handler_ =
      g_signal_connect_after(scroller_, "size-allocate",
                             G_CALLBACK(OnViewportSizeAllocate), this);

  // Measure native chrome with the current application/OS font and palette.
  RefreshPresentationSettings();
  RefreshFullscreenAction();
  applying_mirror_frame_ = true;
  gtk_window_set_default_size(GTK_WINDOW(window_),
                               desktop->restore_client_width(),
                               desktop->restore_client_height() + NativeChromeHeight());
  if (desktop->HasExplicitPosition() && NativeGlobalPositionAvailable()) {
    gtk_window_move(GTK_WINDOW(window_), desktop->x(), desktop->y());
    gtk_window_set_geometry_hints(GTK_WINDOW(window_), nullptr, nullptr,
                                  GDK_HINT_USER_POS);
  }
  applying_mirror_frame_ = false;
  SyncTitleFromMirror();
  ApplyLocalPlacement();
  gtk_widget_show_all(window_);
  RefreshLayout();
}

void LinuxDesktopWindowPresenter::SyncTitleFromMirror() {
  std::string const title_text = ResolveTitleText();
  char const* current = gtk_window_get_title(GTK_WINDOW(window_));
  if (!current || title_text != current)
    gtk_window_set_title(GTK_WINDOW(window_), title_text.c_str());
}

void LinuxDesktopWindowPresenter::OnModelChanged() {
  SyncTitleFromMirror();
  RefreshPresentationSettings();
  ApplyLocalPlacement();
  ApplyNativeFullscreen();
  RefreshLayout();
}

void LinuxDesktopWindowPresenter::DisconnectNativeFrameReports() {
  if (viewport_resize_idle_) {
    g_source_remove(viewport_resize_idle_);
    viewport_resize_idle_ = 0;
  }
  if (configure_handler_ != 0) {
    g_signal_handler_disconnect(window_, configure_handler_);
    configure_handler_ = 0;
  }
  if (size_allocate_handler_ != 0) {
    g_signal_handler_disconnect(scroller_, size_allocate_handler_);
    size_allocate_handler_ = 0;
  }
}

void LinuxDesktopWindowPresenter::OnUnload() {
  if (layout_idle_) { g_source_remove(layout_idle_); layout_idle_ = 0; }
  for (auto const& value : monitor_subscriptions_) {
    g_signal_handler_disconnect(value.monitor, value.handler);
    g_object_unref(value.monitor);
  }
  monitor_subscriptions_.clear();
  auto* display = gtk_widget_get_display(window_);
  g_signal_handler_disconnect(display, monitor_added_handler_);
  g_signal_handler_disconnect(display, monitor_removed_handler_);
  monitor_added_handler_ = monitor_removed_handler_ = 0;
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
  g_signal_handlers_disconnect_by_data(window_, this);
  gtk_widget_destroy(window_);
  window_ = nullptr;
  client_mount_ = nullptr;
  scroller_ = nullptr;
  presentation_menu_ = nullptr;
  fullscreen_toolbar_ = fullscreen_action_ = nullptr;
  applied_appearance_.reset();
  applied_environment_.reset();
  last_reported_window_.reset();
  mapped_ = false;
}

}  // namespace apptraverse::ui::linux
