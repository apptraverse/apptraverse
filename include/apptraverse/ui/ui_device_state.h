#pragma once

#include <string>
#include <optional>
#include <set>
#include <vector>
#include "apptraverse/client_local_state.h"
#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"

namespace apptraverse::ui {

enum class UiThemeMode : std::uint8_t { kAuto, kLight, kDark };
struct UiAppearancePreferences {
  UiThemeMode theme{UiThemeMode::kAuto};
  bool soften_dark{false};
  AE_REFLECT_MEMBERS(theme, soften_dark)
  bool operator==(UiAppearancePreferences const&) const = default;
};
struct UiDisplayRect {
  std::int32_t x{}, y{}, width{}, height{};
  AE_REFLECT_MEMBERS(x, y, width, height)
  bool operator==(UiDisplayRect const&) const = default;
};
struct UiMonitorEnvironment {
  // OS display endpoint name; meaningful only within this installation.
  std::string key;
  UiDisplayRect bounds, work_area;
  bool primary{};
  std::uint32_t orientation{}, refresh_hz{};
  AE_REFLECT_MEMBERS(key, bounds, work_area, primary, orientation, refresh_hz)
  bool operator==(UiMonitorEnvironment const&) const = default;
};
struct UiSystemFont {
  std::string family;
  std::int32_t height{}, weight{};
  bool italic{};
  AE_REFLECT_MEMBERS(family, height, weight, italic)
  bool operator==(UiSystemFont const&) const = default;
};
enum UiEnvironmentCapability : std::uint32_t {
  kSystemTextScale = 1 << 0, kSystemDarkMode = 1 << 1,
  kSystemHighContrast = 1 << 2, kSystemReducedMotion = 1 << 3,
  kSystemKeyboardCues = 1 << 4, kSystemScreenReaderHint = 1 << 5,
};
struct UiDeviceEnvironment {
  std::uint32_t supported_settings{};
  std::uint32_t system_dpi_x{96}, system_dpi_y{96};
  // Fixed-point to avoid floating noise in repeated OS notifications.
  std::uint32_t text_scale_milli{1000};
  bool dark{}, high_contrast{}, reduced_motion{}, keyboard_cues{}, screen_reader_hint{};
  UiSystemFont message_font;
  std::vector<std::uint32_t> colors;
  std::vector<UiMonitorEnvironment> monitors;
  AE_REFLECT_MEMBERS(supported_settings, system_dpi_x, system_dpi_y, text_scale_milli, dark,
                     high_contrast, reduced_motion, keyboard_cues,
                     screen_reader_hint, message_font, colors, monitors)
  bool operator==(UiDeviceEnvironment const&) const = default;
};
struct UiWindowEnvironment {
  ae::ObjId window_id;
  std::string monitor_key;
  std::uint32_t dpi_x{96}, dpi_y{96};
  // Desktop: outer frame in 96-DIPs relative to the monitor work area.
  // Android/Web: logical client surface at 0,0 (Android dp/CSS px).
  // See ClientLocalEnvironment.md; this is not a physical desktop frame there.
  UiDisplayRect frame_dip;
  AE_REFLECT_MEMBERS(window_id, monitor_key, dpi_x, dpi_y, frame_dip)
  bool operator==(UiWindowEnvironment const&) const = default;
};
enum class UiEnvironmentReason : std::uint8_t { kStartup, kSystemNotification, kUserWindowChange };
enum class UiMonitorResolution : std::uint8_t { kFound, kMissing, kUnavailable };
struct UiWindowPlacement {
  ae::ObjId window_id;
  std::string preferred_monitor, resolved_monitor;
  UiDisplayRect frame_dip;
  UiMonitorResolution resolution{UiMonitorResolution::kFound};
  std::uint64_t revision{};
  AE_REFLECT_MEMBERS(window_id, preferred_monitor, resolved_monitor,
                     frame_dip, resolution, revision)
};
enum UiEnvironmentChange : std::uint32_t {
  kEnvironmentInitialized = 1 << 0,
  kEnvironmentDpi = 1 << 1,
  kEnvironmentDisplays = 1 << 2,
  kEnvironmentResolution = 1 << 3,
  kEnvironmentWorkArea = 1 << 4,
  kEnvironmentText = 1 << 5,
  kEnvironmentColors = 1 << 6,
  kEnvironmentAccessibility = 1 << 7,
};
std::uint32_t CompareUiDeviceEnvironment(UiDeviceEnvironment const& before,
                                        UiDeviceEnvironment const& after);
// Enumeration order is not an environmental change.
void NormalizeUiDeviceEnvironment(UiDeviceEnvironment& environment);

class UiDeviceEnvironmentChangedEvent;
class UiWindowEnvironmentChangedEvent;
class SetUiAppearancePreferencesEvent;
class UiWindowMonitorResolvedEvent;
class ForgetUiWindowEnvironmentEvent;

class UiDeviceState : public NodeFor<UiDeviceState, ClientLocalState> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiDeviceState",
                          UiDeviceState, ClientLocalState, 0)
 protected:
  UiDeviceState() = default;
 public:
  explicit UiDeviceState(ae::ObjProp prop)
      : NodeFor{prop}, has_environment{false}, observation_revision{0},
        last_changes{0}, last_reason{UiEnvironmentReason::kStartup} {}
  AE_OBJECT_REFLECT(AE_MMBR(appearance), AE_MMBR(has_environment),
                    AE_MMBR(environment), AE_MMBR(windows),
                    AE_MMBR(observation_revision), AE_MMBR(last_changes),
                    AE_MMBR(last_reason), AE_MMBR(placements))
  template <typename Dnv> void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, appearance, has_environment, environment, windows,
        observation_revision, last_changes, last_reason, placements);
  }
  template <typename Dnv> void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, appearance, has_environment, environment, windows,
        observation_revision, last_changes, last_reason, placements);
  }
  UiAppearancePreferences appearance;
  bool has_environment;
  UiDeviceEnvironment environment;
  std::vector<UiWindowEnvironment> windows;
  std::uint64_t observation_revision;
  std::uint32_t last_changes;
  UiEnvironmentReason last_reason;
  std::vector<UiWindowPlacement> placements;

  void Bootstrap(UiAppearancePreferences preferences, UiDeviceEnvironment baseline) {
    appearance = preferences;
    has_environment = false;
    environment = std::move(baseline);
    windows.clear();
    placements.clear();
    observation_revision = 0;
    last_changes = 0;
    last_reason = UiEnvironmentReason::kStartup;
  }
  void ObserveEnvironment(UiDeviceEnvironment current, UiEnvironmentReason reason);
  void ObserveWindow(UiWindowEnvironment current, UiEnvironmentReason reason);
  void SetAppearance(UiAppearancePreferences preferences);
  // Optional model-owner lifecycle policy. It is runtime-only: restoring/importing
  // the owner's live identities determines which late native reports survive.
  void SetWindowObservationTargets(std::set<ae::ObjId> targets) {
    window_observation_targets_ = std::move(targets);
  }
  void AllowWindowObservation(ae::ObjId window_id) {
    if (window_observation_targets_) window_observation_targets_->insert(window_id);
  }
  void ForgetWindow(ae::ObjId window_id);
  virtual std::set<ae::ObjId> WindowStateIds() const;
  void Apply(UiDeviceEnvironmentChangedEvent const&);
  void Apply(UiWindowEnvironmentChangedEvent const&);
  void Apply(SetUiAppearancePreferencesEvent const&);
  void Apply(UiWindowMonitorResolvedEvent const&);
  void Apply(ForgetUiWindowEnvironmentEvent const&);
  UiWindowPlacement const* PlacementFor(ae::ObjId window_id) const;
 protected:
  bool AcceptsWindowObservation(ae::ObjId window_id) const {
    return !window_observation_targets_ || window_observation_targets_->contains(window_id);
  }
  virtual void ForgetWindowState(ae::ObjId) {}
 private:
  std::optional<std::set<ae::ObjId>> window_observation_targets_;
  void ResolveWindowMonitors(UiEnvironmentReason reason, bool layout_changed);
};

class ForgetUiWindowEnvironmentEvent
    : public EventFor<UiDeviceState, ForgetUiWindowEnvironmentEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::ForgetUiWindowEnvironmentEvent",
                          ForgetUiWindowEnvironmentEvent, Event, 0)
 protected:
  ForgetUiWindowEnvironmentEvent() = default;
 public:
  explicit ForgetUiWindowEnvironmentEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(window_id))
  ae::ObjId window_id;
};

class UiDeviceEnvironmentChangedEvent
    : public EventFor<UiDeviceState, UiDeviceEnvironmentChangedEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiDeviceEnvironmentChangedEvent",
                          UiDeviceEnvironmentChangedEvent, Event, 0)
 protected:
  UiDeviceEnvironmentChangedEvent() = default;
 public:
  explicit UiDeviceEnvironmentChangedEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(had_previous), AE_MMBR(previous), AE_MMBR(current),
                    AE_MMBR(changes), AE_MMBR(reason))
  bool had_previous{};
  UiDeviceEnvironment previous, current;
  std::uint32_t changes{};
  UiEnvironmentReason reason{};
};
class UiWindowEnvironmentChangedEvent
    : public EventFor<UiDeviceState, UiWindowEnvironmentChangedEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiWindowEnvironmentChangedEvent",
                          UiWindowEnvironmentChangedEvent, Event, 0)
 protected:
  UiWindowEnvironmentChangedEvent() = default;
 public:
  explicit UiWindowEnvironmentChangedEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(had_previous), AE_MMBR(previous), AE_MMBR(current),
                    AE_MMBR(reason), AE_MMBR(update_preference))
  bool had_previous{};
  UiWindowEnvironment previous, current;
  UiEnvironmentReason reason{};
  bool update_preference{};
};
class UiWindowMonitorResolvedEvent
    : public EventFor<UiDeviceState, UiWindowMonitorResolvedEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiWindowMonitorResolvedEvent",
                          UiWindowMonitorResolvedEvent, Event, 0)
 protected:
  UiWindowMonitorResolvedEvent() = default;
 public:
  explicit UiWindowMonitorResolvedEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(window_id), AE_MMBR(preferred_monitor),
                    AE_MMBR(resolved_monitor), AE_MMBR(resolution), AE_MMBR(reason))
  ae::ObjId window_id;
  std::string preferred_monitor, resolved_monitor;
  UiMonitorResolution resolution{};
  UiEnvironmentReason reason{};
};
class SetUiAppearancePreferencesEvent
    : public EventFor<UiDeviceState, SetUiAppearancePreferencesEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::SetUiAppearancePreferencesEvent",
                          SetUiAppearancePreferencesEvent, Event, 0)
 protected:
  SetUiAppearancePreferencesEvent() = default;
 public:
  explicit SetUiAppearancePreferencesEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(preferences))
  UiAppearancePreferences preferences;
};
}  // namespace apptraverse::ui
