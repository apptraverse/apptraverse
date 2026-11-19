#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include "apptraverse/distill.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/single_file_user_storage.h"
#include "apptraverse/ui/ui_device_state.h"
#include "apptraverse/ui/ui_presenters.h"

using namespace apptraverse;
using namespace apptraverse::ui;
#define CHECK(c) do { if (!(c)) { std::cerr << "CHECK " #c << " line " << __LINE__ << '\n'; std::exit(1); } } while (false)

namespace {
ae::ObjId const kLocal{100};
UiDeviceEnvironment Environment() {
  UiDeviceEnvironment value;
  value.message_font = {"Example Font", -12, 400, false};
  value.colors = {0xFFFFFF, 0};
  value.monitors = {{"display-a", {0, 0, 1920, 1080}, {0, 0, 1920, 1040}, true, 0, 60}};
  return value;
}
void Supply(ae::RamDomainStorage& supply) {
  ae::Domain domain{supply};
  auto root = UiDeviceState::ptr::Create(ae::CreateWith{domain}.with_id(kLocal));
  root->Bootstrap({UiThemeMode::kAuto, false}, UiDeviceEnvironment{});
  FinalizeDistilledGraph(*root);
  SaveDistilledRoot(*root);
}
void Save(UiDeviceState& root) {
  auto held = UiDeviceState::ptr::MakeFromThis(&root);
  ae::DomainGraph graph{root.domain};
  auto const result = graph.SaveRoot(held.cached(), root.obj_id);
  CHECK(result.IsOk());
}

void TestObservationEventsAndReplay() {
  ae::RamDomainStorage supply, portable, local;
  Supply(supply);
  auto roots = CollectClientLocalRoots(supply);
  CHECK(roots.contains(kLocal));
  ClientLocalDomainStorage routed{portable, local, supply, roots};
  OverlayDomainStorage overlay{routed, supply};
  ae::Domain domain{overlay};
  auto state = LoadApplication<UiDeviceState>(domain, kLocal);
  auto current = Environment();
  state->ObserveEnvironment(current, UiEnvironmentReason::kStartup);
  CHECK(state->last_changes == kEnvironmentInitialized);
  CHECK(state->observation_revision == 1);
  auto event = state->journal.back().event;
  CHECK(event->GetClassId() == UiDeviceEnvironmentChangedEvent::kClassId);
  auto const& initial = *static_cast<UiDeviceEnvironmentChangedEvent const*>(&*event);
  CHECK(!initial.had_previous);
  CHECK(initial.reason == UiEnvironmentReason::kStartup);
  auto generation = state->Generation();
  state->ObserveEnvironment(current, UiEnvironmentReason::kSystemNotification);
  CHECK(state->Generation() == generation);
  current.system_dpi_x = current.system_dpi_y = 144;
  current.monitors[0].bounds.width = 2560;
  current.monitors[0].work_area.width = 2560;
  current.text_scale_milli = 1500;
  current.high_contrast = true;
  current.dark = true;
  state->ObserveEnvironment(current, UiEnvironmentReason::kSystemNotification);
  CHECK(state->last_changes == (kEnvironmentDpi | kEnvironmentResolution |
      kEnvironmentWorkArea | kEnvironmentText | kEnvironmentAccessibility | kEnvironmentColors));
  auto const& changed = *static_cast<UiDeviceEnvironmentChangedEvent const*>(
      &*state->journal.back().event);
  CHECK(changed.had_previous);
  CHECK(changed.previous.system_dpi_x == 96);
  CHECK(changed.current.system_dpi_x == 144);
  current.monitors.push_back({"display-b", {-1280, 0, 1280, 720}, {-1280, 0, 1280, 720}, false, 0, 60});
  state->ObserveEnvironment(current, UiEnvironmentReason::kSystemNotification);
  CHECK(state->last_changes == kEnvironmentDisplays);
  generation = state->Generation();
  std::reverse(current.monitors.begin(), current.monitors.end());
  state->ObserveEnvironment(current, UiEnvironmentReason::kSystemNotification);
  CHECK(state->Generation() == generation);
  state->ObserveWindow({ae::ObjId{2}, "display-a", 144, 144}, UiEnvironmentReason::kStartup);
  auto const& first_window = *static_cast<UiWindowEnvironmentChangedEvent const*>(
      &*state->journal.back().event);
  CHECK(!first_window.had_previous);
  CHECK(first_window.previous == UiWindowEnvironment{});
  state->ObserveWindow({ae::ObjId{3}, "display-b", 96, 96}, UiEnvironmentReason::kStartup);
  state->ObserveWindow({ae::ObjId{2}, "display-b", 96, 96}, UiEnvironmentReason::kSystemNotification);
  CHECK(state->windows.size() == 2);
  auto const& moved = *static_cast<UiWindowEnvironmentChangedEvent const*>(
      &*state->journal.back().event);
  CHECK(moved.previous.dpi_x == 144 && moved.current.dpi_x == 96);
  state->SetAppearance({UiThemeMode::kDark, true});
  auto const replayed = state->TryReplayFromBase();
  CHECK(replayed);
  CHECK(state->appearance.soften_dark);
  CHECK(state->environment.monitors.size() == 2);
  CHECK(state->windows[0].dpi_x == 96);
  Save(*state);
  CHECK(portable.state.empty());
  CHECK(!local.state.empty());
  for (auto const& [id, _] : local.state) {
    auto scope = local.ReadObjectScope(id);
    CHECK(scope && scope->root_id == kLocal);
  }
}

void TestColdStartupAndTransfer() {
  ae::RamDomainStorage supply, portable, local;
  Supply(supply);
  auto const original_supply = supply.state;
  auto const roots = CollectClientLocalRoots(supply);
  {
    ClientLocalDomainStorage routed{portable, local, supply, roots};
    OverlayDomainStorage overlay{routed, supply};
    ae::Domain domain{overlay};
    auto state = LoadApplication<UiDeviceState>(domain, kLocal);
    state->ObserveEnvironment(Environment(), UiEnvironmentReason::kStartup);
    state->SetAppearance({UiThemeMode::kDark, true});
    Save(*state);
  }
  auto profile = std::filesystem::temp_directory_path() /
      ("apptraverse-device-state-" + std::to_string(ae::ObjId::GenerateUnique().id()));
  auto const saved = SaveUserStorageFile(local, profile / "local");
  CHECK(saved);
  ae::RamDomainStorage cold;
  auto const loaded = LoadUserStorageFile(cold, profile / "local");
  CHECK(loaded);
  {
    ClientLocalDomainStorage routed{portable, cold, supply, roots};
    OverlayDomainStorage overlay{routed, supply};
    ae::Domain domain{overlay};
    auto state = LoadApplication<UiDeviceState>(domain, kLocal);
    CHECK(state->appearance.soften_dark);
    auto generation = state->Generation();
    state->ObserveEnvironment(Environment(), UiEnvironmentReason::kStartup);
    CHECK(state->Generation() == generation);
    auto changed = Environment();
    changed.system_dpi_x = changed.system_dpi_y = 192;
    changed.monitors.clear();
    state->ObserveEnvironment(changed, UiEnvironmentReason::kStartup);
    CHECK(state->last_reason == UiEnvironmentReason::kStartup);
    CHECK(state->last_changes == (kEnvironmentDpi | kEnvironmentDisplays));
    auto const& event = *static_cast<UiDeviceEnvironmentChangedEvent const*>(
        &*state->journal.back().event);
    CHECK(event.previous.monitors.size() == 1 && event.current.monitors.empty());
    Save(*state);
  }
  ae::RamDomainStorage destination;
  SeedClientLocalStorage(destination, cold);
  CHECK(destination.state == cold.state);
  auto const own_state = destination.state;
  SeedClientLocalStorage(destination, local);
  CHECK(destination.state == own_state);
  CHECK(portable.state.empty());
  CHECK(supply.state == original_supply);
  std::filesystem::remove(profile / "local" / "user.bin");
  std::filesystem::remove(profile / "local");
  std::filesystem::remove(profile);
}
void TestMonitorLossAndReturnWithImmediateCompaction() {
  ae::RamDomainStorage supply, portable, local;
  Supply(supply);
  ClientLocalDomainStorage routed{portable, local, supply, CollectClientLocalRoots(supply)};
  OverlayDomainStorage overlay{routed, supply};
  ae::Domain domain{overlay};
  auto state = LoadApplication<UiDeviceState>(domain, kLocal);
  state->SetImmediateEventCompaction(true);
  auto two = Environment();
  two.monitors.push_back({"display-b", {1920, 0, 2560, 1440}, {1920, 0, 2560, 1400}, false, 0, 60});
  state->ObserveEnvironment(two, UiEnvironmentReason::kStartup);
  state->ObserveWindow({ae::ObjId{2}, "display-b", 144, 144, {100, 50, 600, 400}}, UiEnvironmentReason::kStartup);
  state->ObserveWindow({ae::ObjId{3}, "display-a", 96, 96, {10, 20, 500, 300}}, UiEnvironmentReason::kStartup);
  state->ObserveEnvironment(Environment(), UiEnvironmentReason::kSystemNotification);
  CHECK(state->PlacementFor(ae::ObjId{2})->resolution == UiMonitorResolution::kMissing);
  CHECK(state->PlacementFor(ae::ObjId{2})->resolved_monitor == "display-a");
  // Applying a fallback reports native facts but must preserve the preference.
  state->ObserveWindow({ae::ObjId{2}, "display-a", 96, 96, {100, 50, 600, 400}},
                       UiEnvironmentReason::kSystemNotification);
  CHECK(state->PlacementFor(ae::ObjId{2})->preferred_monitor == "display-b");
  state->ObserveEnvironment(two, UiEnvironmentReason::kSystemNotification);
  CHECK(state->PlacementFor(ae::ObjId{2})->resolution == UiMonitorResolution::kFound);
  CHECK(state->PlacementFor(ae::ObjId{2})->resolved_monitor == "display-b");
  // A deliberate user move adopts the new monitor as the preference.
  state->ObserveWindow({ae::ObjId{2}, "display-a", 96, 96, {20, 30, 600, 400}},
                       UiEnvironmentReason::kUserWindowChange);
  CHECK(state->PlacementFor(ae::ObjId{2})->preferred_monitor == "display-a");
  auto unavailable = two;
  unavailable.monitors.clear();
  state->ObserveEnvironment(unavailable, UiEnvironmentReason::kSystemNotification);
  CHECK(state->PlacementFor(ae::ObjId{2})->resolution == UiMonitorResolution::kUnavailable);
  state->ObserveEnvironment(two, UiEnvironmentReason::kSystemNotification);
  CHECK(state->PlacementFor(ae::ObjId{2})->resolution == UiMonitorResolution::kFound);
  Save(*state);
  CHECK(portable.state.empty());
}
void TestQueuedObservationAfterMonitorLoss() {
  ae::RamDomainStorage supply, portable, local;
  Supply(supply);
  ClientLocalDomainStorage routed{portable, local, supply, CollectClientLocalRoots(supply)};
  OverlayDomainStorage overlay{routed, supply};
  ae::Domain domain{overlay};
  auto state = LoadApplication<UiDeviceState>(domain, kLocal);
  for (bool compact : {false, true}) {
    state->SetImmediateEventCompaction(compact);
    state->ObserveEnvironment(Environment(), UiEnvironmentReason::kSystemNotification);
    // A queued first observation arrives after its monitor disappeared.
    ae::ObjId const window{compact ? 41u : 40u};
    state->ObserveWindow({window, "removed", 192, 192, {20, 30, 600, 400}}, UiEnvironmentReason::kStartup);
    CHECK(state->PlacementFor(window)->resolution == UiMonitorResolution::kMissing);
    CHECK(state->PlacementFor(window)->resolved_monitor == "display-a");
    CHECK(state->PlacementFor(window)->preferred_monitor == "removed");
    CHECK(state->PlacementFor(window)->revision == 2);
    auto empty = Environment(); empty.monitors.clear();
    state->ObserveEnvironment(empty, UiEnvironmentReason::kSystemNotification);
    state->ObserveWindow({window, "removed", 144, 144, {40, 50, 500, 300}}, UiEnvironmentReason::kUserWindowChange);
    CHECK(state->PlacementFor(window)->resolution == UiMonitorResolution::kUnavailable);
    CHECK(state->PlacementFor(window)->resolved_monitor.empty());
    CHECK(state->PlacementFor(window)->preferred_monitor == "removed");
    CHECK(state->TryReplayFromBase());
    CHECK(state->PlacementFor(window)->resolution == UiMonitorResolution::kUnavailable);
    CHECK(state->PlacementFor(window)->frame_dip == (UiDisplayRect{40, 50, 500, 300}));
  }
  Save(*state);
  CHECK(portable.state.empty());
}
void TestThreeMonitorPreferenceAndDeletionColdRestore() {
  for (bool compact : {false, true}) {
    ae::RamDomainStorage supply, portable, local;
    Supply(supply);
    auto const roots = CollectClientLocalRoots(supply);
    ae::ObjId const first{81}, second{82};
    UiDeviceEnvironment three = Environment();
    three.monitors.push_back({"display-b", {1920, 0, 1280, 720}, {1920, 0, 1280, 680}, false, 0, 60});
    three.monitors.push_back({"display-c", {-1280, 0, 1280, 720}, {-1280, 0, 1280, 680}, false, 0, 60});
    UiDisplayRect const preferred{80, 90, 900, 700};
    {
      ClientLocalDomainStorage routed{portable, local, supply, roots};
      OverlayDomainStorage overlay{routed, supply}; ae::Domain domain{overlay};
      auto state = LoadApplication<UiDeviceState>(domain, kLocal);
      state->SetImmediateEventCompaction(compact);
      state->SetWindowObservationTargets({first, second});
      state->ObserveEnvironment(three, UiEnvironmentReason::kStartup);
      state->ObserveWindow({first, "display-b", 144, 144, preferred}, UiEnvironmentReason::kStartup);
      state->ObserveWindow({second, "display-c", 96, 96, {10, 20, 500, 300}}, UiEnvironmentReason::kStartup);
      auto missing = three; missing.monitors.erase(missing.monitors.begin() + 1);
      state->ObserveEnvironment(missing, UiEnvironmentReason::kSystemNotification);
      CHECK(state->PlacementFor(first)->resolved_monitor == "display-a");
      CHECK(state->PlacementFor(second)->resolved_monitor == "display-c");
      // System clamping on a smaller fallback is factual, not a new preference.
      state->ObserveWindow({first, "display-a", 96, 96, {0, 0, 640, 480}}, UiEnvironmentReason::kSystemNotification);
      CHECK(state->PlacementFor(first)->frame_dip == preferred);
      state->ObserveEnvironment(three, UiEnvironmentReason::kSystemNotification);
      CHECK(state->PlacementFor(first)->resolved_monitor == "display-b");
      CHECK(state->PlacementFor(first)->frame_dip == preferred);
      state->ObserveEnvironment(missing, UiEnvironmentReason::kSystemNotification);
      // The user chooses a third display while the old preference is missing.
      UiDisplayRect const override_frame{30, 40, 650, 450};
      state->ObserveWindow({first, "display-c", 120, 120, override_frame}, UiEnvironmentReason::kUserWindowChange);
      state->ObserveEnvironment(three, UiEnvironmentReason::kSystemNotification);
      CHECK(state->PlacementFor(first)->preferred_monitor == "display-c");
      CHECK(state->PlacementFor(first)->resolved_monitor == "display-c");
      CHECK(state->PlacementFor(first)->frame_dip == override_frame);
      state->ForgetWindow(second);
      CHECK(!state->PlacementFor(second));
      auto generation = state->Generation();
      state->ObserveWindow({second, "display-b", 96, 96, preferred}, UiEnvironmentReason::kUserWindowChange);
      CHECK(state->Generation() == generation);
      CHECK(state->windows.size() == 1 && state->windows.front().window_id == first);
      CHECK(state->placements.size() == 1 && state->placements.front().window_id == first);
      CHECK(state->TryReplayFromBase());
      CHECK(!state->PlacementFor(second));
      CHECK(state->PlacementFor(first)->preferred_monitor == "display-c");
      Save(*state);
    }
    {
      ClientLocalDomainStorage routed{portable, local, supply, roots};
      OverlayDomainStorage overlay{routed, supply}; ae::Domain domain{overlay};
      auto state = LoadApplication<UiDeviceState>(domain, kLocal);
      state->SetWindowObservationTargets({first});
      auto startup = three; startup.monitors.pop_back();
      state->ObserveEnvironment(startup, UiEnvironmentReason::kStartup);
      CHECK(state->PlacementFor(first)->resolution == UiMonitorResolution::kMissing);
      CHECK(state->PlacementFor(first)->resolved_monitor == "display-a");
      CHECK(state->PlacementFor(first)->preferred_monitor == "display-c");
      CHECK(!state->PlacementFor(second));
      state->ObserveEnvironment(three, UiEnvironmentReason::kSystemNotification);
      CHECK(state->PlacementFor(first)->resolved_monitor == "display-c");
      CHECK(state->PlacementFor(first)->frame_dip == (UiDisplayRect{30, 40, 650, 450}));
    }
    CHECK(portable.state.empty());
  }
}

}
int main() {
  EnsureCoreObjectRegistration();
  EnsureUiControlRegistration();
  TestQueuedObservationAfterMonitorLoss();
  TestThreeMonitorPreferenceAndDeletionColdRestore();
  TestObservationEventsAndReplay();
  TestColdStartupAndTransfer();
  TestMonitorLossAndReturnWithImmediateCompaction();
  std::cout << "ui_device_state_test OK\n";
}
