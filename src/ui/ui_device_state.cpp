#include "apptraverse/ui/ui_device_state.h"
#include <algorithm>

namespace apptraverse::ui {
void NormalizeUiDeviceEnvironment(UiDeviceEnvironment& value) {
  std::sort(value.monitors.begin(), value.monitors.end(),
            [](auto const& a, auto const& b) { return a.key < b.key; });
}
std::uint32_t CompareUiDeviceEnvironment(UiDeviceEnvironment const& a,
                                        UiDeviceEnvironment const& b) {
  std::uint32_t changes{};
  auto const capabilities_changed = a.supported_settings ^ b.supported_settings;
  if (capabilities_changed & kSystemTextScale) changes |= kEnvironmentText;
  if (capabilities_changed & kSystemDarkMode) changes |= kEnvironmentColors;
  if (capabilities_changed & ~(kSystemTextScale | kSystemDarkMode)) changes |= kEnvironmentAccessibility;
  if (a.system_dpi_x != b.system_dpi_x || a.system_dpi_y != b.system_dpi_y)
    changes |= kEnvironmentDpi;
  if (a.text_scale_milli != b.text_scale_milli || a.message_font != b.message_font)
    changes |= kEnvironmentText;
  if (a.dark != b.dark || a.colors != b.colors) changes |= kEnvironmentColors;
  if (a.high_contrast != b.high_contrast || a.reduced_motion != b.reduced_motion ||
      a.keyboard_cues != b.keyboard_cues || a.screen_reader_hint != b.screen_reader_hint)
    changes |= kEnvironmentAccessibility;
  if (a.monitors.size() != b.monitors.size()) changes |= kEnvironmentDisplays;
  for (auto const& monitor : b.monitors) {
    auto old = std::find_if(a.monitors.begin(), a.monitors.end(),
                           [&](auto const& item) { return item.key == monitor.key; });
    if (old == a.monitors.end()) { changes |= kEnvironmentDisplays; continue; }
    if (old->primary != monitor.primary) changes |= kEnvironmentDisplays;
    if (old->bounds != monitor.bounds || old->orientation != monitor.orientation ||
        old->refresh_hz != monitor.refresh_hz) changes |= kEnvironmentResolution;
    if (old->work_area != monitor.work_area) changes |= kEnvironmentWorkArea;
  }
  return changes;
}
void UiDeviceState::ObserveEnvironment(UiDeviceEnvironment current, UiEnvironmentReason reason) {
  NormalizeUiDeviceEnvironment(current);
  auto changes = has_environment ? CompareUiDeviceEnvironment(environment, current)
                                 : kEnvironmentInitialized;
  if (changes == 0) {
    ResolveWindowMonitors(reason, false);
    return;
  }
  auto event = UiDeviceEnvironmentChangedEvent::ptr::Create(ae::CreateWith{*domain});
  event->had_previous = has_environment;
  event->previous = environment;
  event->current = std::move(current);
  event->changes = changes;
  event->reason = reason;
  CommitLocal(event);
  ResolveWindowMonitors(reason, (changes & (kEnvironmentDpi | kEnvironmentDisplays |
      kEnvironmentResolution | kEnvironmentWorkArea)) != 0);
}
void UiDeviceState::ObserveWindow(UiWindowEnvironment current, UiEnvironmentReason reason) {
  if (!AcceptsWindowObservation(current.window_id)) return;
  auto old = std::find_if(windows.begin(), windows.end(), [&](auto const& item) {
    return item.window_id == current.window_id;
  });
  auto const* placement = PlacementFor(current.window_id);
  bool const update_preference = !placement || reason == UiEnvironmentReason::kUserWindowChange;
  bool const preference_changed = !placement ||
      placement->preferred_monitor != current.monitor_key || placement->frame_dip != current.frame_dip;
  if (old != windows.end() && *old == current && !(update_preference && preference_changed)) return;
  auto event = UiWindowEnvironmentChangedEvent::ptr::Create(ae::CreateWith{*domain});
  event->had_previous = old != windows.end();
  if (event->had_previous) event->previous = *old;
  event->current = std::move(current);
  event->reason = reason;
  event->update_preference = update_preference;
  CommitLocal(event);
  ResolveWindowMonitors(reason, false);
}
UiWindowPlacement const* UiDeviceState::PlacementFor(ae::ObjId window_id) const {
  auto found = std::find_if(placements.begin(), placements.end(),
      [&](auto const& item) { return item.window_id == window_id; });
  return found == placements.end() ? nullptr : &*found;
}
void UiDeviceState::ResolveWindowMonitors(UiEnvironmentReason reason, bool layout_changed) {
  auto const monitors = environment.monitors;
  auto primary = std::find_if(monitors.begin(), monitors.end(),
                             [](auto const& item) { return item.primary; });
  if (primary == monitors.end()) primary = monitors.begin();
  // Immediate compaction may rehydrate state during Commit. Keep iteration
  // independent of the live rows and monitor storage.
  auto const pending_placements = placements;
  for (auto const& placement : pending_placements) {
    auto found = std::find_if(monitors.begin(), monitors.end(),
        [&](auto const& item) { return item.key == placement.preferred_monitor; });
    auto const status = monitors.empty() ? UiMonitorResolution::kUnavailable
        : found == monitors.end() ? UiMonitorResolution::kMissing : UiMonitorResolution::kFound;
    auto const selected_key = monitors.empty() ? std::string{}
        : found == monitors.end() ? primary->key : found->key;
    if (!layout_changed && placement.resolution == status &&
        placement.resolved_monitor == selected_key) continue;
    auto event = UiWindowMonitorResolvedEvent::ptr::Create(ae::CreateWith{*domain});
    event->window_id = placement.window_id;
    event->preferred_monitor = placement.preferred_monitor;
    event->resolved_monitor = selected_key;
    event->resolution = status;
    event->reason = reason;
    CommitLocal(event);
  }
}
void UiDeviceState::SetAppearance(UiAppearancePreferences preferences) {
  if (appearance == preferences) return;
  auto event = SetUiAppearancePreferencesEvent::ptr::Create(ae::CreateWith{*domain});
  event->preferences = preferences;
  CommitLocal(event);
}
void UiDeviceState::ForgetWindow(ae::ObjId window_id) {
  if (window_observation_targets_) window_observation_targets_->erase(window_id);
  // This Event also gives derived LOCAL state one deletion boundary, even when
  // this window has no native geometry yet (for example fullscreen before Show).
  auto event = ForgetUiWindowEnvironmentEvent::ptr::Create(ae::CreateWith{*domain});
  event->window_id = window_id;
  CommitLocal(event);
}
std::set<ae::ObjId> UiDeviceState::WindowStateIds() const {
  std::set<ae::ObjId> ids;
  for (auto const& item : windows) ids.insert(item.window_id);
  for (auto const& item : placements) ids.insert(item.window_id);
  return ids;
}
void UiDeviceState::Apply(ForgetUiWindowEnvironmentEvent const& event) {
  std::erase_if(windows, [&](auto const& item) { return item.window_id == event.window_id; });
  std::erase_if(placements, [&](auto const& item) { return item.window_id == event.window_id; });
  ForgetWindowState(event.window_id);
  NoteMaterializedChange();
}
void UiDeviceState::Apply(UiDeviceEnvironmentChangedEvent const& event) {
  environment = event.current;
  has_environment = true;
  ++observation_revision;
  last_changes = event.changes;
  last_reason = event.reason;
  NoteMaterializedChange();
}
void UiDeviceState::Apply(UiWindowEnvironmentChangedEvent const& event) {
  auto old = std::find_if(windows.begin(), windows.end(), [&](auto const& item) {
    return item.window_id == event.current.window_id;
  });
  if (old == windows.end()) windows.push_back(event.current);
  else *old = event.current;
  if (event.update_preference) {
    auto placement = std::find_if(placements.begin(), placements.end(), [&](auto const& item) {
      return item.window_id == event.current.window_id;
    });
    if (placement == placements.end()) {
      placements.push_back({});
      placement = std::prev(placements.end());
      placement->window_id = event.current.window_id;
      placement->revision = 1;
    }
    placement->preferred_monitor = event.current.monitor_key;
    placement->resolved_monitor = event.current.monitor_key;
    placement->frame_dip = event.current.frame_dip;
    placement->resolution = UiMonitorResolution::kFound;
    // This observation is the result of an already applied native action.
    // Only a monitor-resolution Event issues a new placement request.
  }
  NoteMaterializedChange();
}
void UiDeviceState::Apply(UiWindowMonitorResolvedEvent const& event) {
  auto placement = std::find_if(placements.begin(), placements.end(), [&](auto const& item) {
    return item.window_id == event.window_id;
  });
  // Resolutions are generated only for already persisted placements.
  assert(placement != placements.end());
  placement->resolved_monitor = event.resolved_monitor;
  placement->resolution = event.resolution;
  ++placement->revision;
  NoteMaterializedChange();
}
void UiDeviceState::Apply(SetUiAppearancePreferencesEvent const& event) {
  appearance = event.preferences;
  NoteMaterializedChange();
}
}  // namespace apptraverse::ui
