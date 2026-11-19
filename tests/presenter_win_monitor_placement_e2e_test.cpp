#include "window_local_environment_fixture.h"
#include <algorithm>

int main() {
  using namespace apptraverse;
  using namespace apptraverse::test;
  using namespace apptraverse::ui;
  using namespace apptraverse::ui::windows;
  NativeLocalWindowHarness h{true};
  auto& local = *h.model->window->device_state;
  WinUiEnvironment reader{h.presenter->hwnd()};
  auto const actual = reader.snapshot();
  auto primary = std::find_if(actual.monitors.begin(), actual.monitors.end(),
                             [](auto const& m) { return m.primary; });
  LOCAL_CHECK(primary != actual.monitors.end());
  if (actual.monitors.size() < 2) {
    std::cout << "presenter_win_monitor_placement_e2e_test SKIP: two real monitors required\n";
    return 77;
  }
  auto secondary = std::find_if(actual.monitors.begin(), actual.monitors.end(),
                               [](auto const& m) { return !m.primary; });
  LOCAL_CHECK(secondary != actual.monitors.end());
  LOCAL_CHECK(primary->key != secondary->key);
  auto& mirror = static_cast<NativeLocalWindowRoot&>(h.session->root());
  auto* other = static_cast<WinDesktopWindowPresenter*>(&*mirror.second_window->presenter);
  SendMessageW(other->hwnd(), WM_ENTERSIZEMOVE, 0, 0);
  LOCAL_CHECK(SetWindowPos(other->hwnd(), nullptr, primary->work_area.x + 150,
      primary->work_area.y + 160, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
  SendMessageW(other->hwnd(), WM_EXITSIZEMOVE, 0, 0);
  h.Pump(); h.Publish();
  auto const other_id = h.model->second_window.id();
  auto const other_preference = *local.PlacementFor(other_id);
  h.Drag(secondary->work_area.x + 40, secondary->work_area.y + 50, 400, 300);
  h.Pump(); h.Publish();
  auto const window_id = h.model->window.id();
  auto preferred = *local.PlacementFor(window_id);
  LOCAL_CHECK(preferred.preferred_monitor == secondary->key);
  LOCAL_CHECK(reader.WindowSnapshot(window_id).monitor_key == secondary->key);
  LOCAL_CHECK(GetWinWindowMonitorKey(h.presenter->hwnd()) == secondary->key);

  // Synthetic model notifications exercise live native placement without
  // changing the desktop configuration of the machine running the test.
  auto missing = actual;
  std::erase_if(missing.monitors, [&](auto const& m) { return m.key == secondary->key; });
  local.ObserveEnvironment(missing, UiEnvironmentReason::kSystemNotification);
  h.Publish(); h.Pump(); h.Publish();
  LOCAL_CHECK(GetWinWindowMonitorKey(h.presenter->hwnd()) == primary->key);
  LOCAL_CHECK(local.PlacementFor(window_id)->preferred_monitor == secondary->key);
  LOCAL_CHECK(local.PlacementFor(window_id)->frame_dip == preferred.frame_dip);
  LOCAL_CHECK(local.PlacementFor(other_id)->preferred_monitor == other_preference.preferred_monitor);
  LOCAL_CHECK(local.PlacementFor(other_id)->frame_dip == other_preference.frame_dip);
  RECT fallback{}; LOCAL_CHECK(GetWindowRect(h.presenter->hwnd(), &fallback));
  SendMessageW(h.presenter->hwnd(), WM_ENTERSIZEMOVE, 0, 0);
  SendMessageW(h.presenter->hwnd(), WM_EXITSIZEMOVE, 0, 0);
  h.Pump(); h.Publish();
  LOCAL_CHECK(local.PlacementFor(window_id)->preferred_monitor == secondary->key);
  // Escape/cancel returns the native frame to its starting geometry before
  // WM_EXITSIZEMOVE. Intermediate movement remains an actual observation only.
  SendMessageW(h.presenter->hwnd(), WM_ENTERSIZEMOVE, 0, 0);
  LOCAL_CHECK(SetWindowPos(h.presenter->hwnd(), nullptr, fallback.left + 30,
      fallback.top + 30, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
  LOCAL_CHECK(SetWindowPos(h.presenter->hwnd(), nullptr, fallback.left,
      fallback.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
  SendMessageW(h.presenter->hwnd(), WM_EXITSIZEMOVE, 0, 0);
  h.Pump(); h.Publish();
  LOCAL_CHECK(local.PlacementFor(window_id)->preferred_monitor == secondary->key);
  LOCAL_CHECK(local.PlacementFor(window_id)->frame_dip == preferred.frame_dip);
  SendMessageW(h.presenter->hwnd(), WM_ENTERSIZEMOVE, 0, 0);
  local.ObserveEnvironment(actual, UiEnvironmentReason::kSystemNotification);
  h.Publish();
  LOCAL_CHECK(GetWinWindowMonitorKey(h.presenter->hwnd()) == primary->key);
  SendMessageW(h.presenter->hwnd(), WM_EXITSIZEMOVE, 0, 0);
  // No motion means there need not be another observation to publish. The
  // return revision deferred during the modal loop must apply on release.
  LOCAL_CHECK(GetWinWindowMonitorKey(h.presenter->hwnd()) == secondary->key);
  h.Pump(); h.Publish();
  LOCAL_CHECK(GetWinWindowMonitorKey(h.presenter->hwnd()) == secondary->key);
  LOCAL_CHECK(local.PlacementFor(window_id)->frame_dip == preferred.frame_dip);

  // A maximized window follows the same monitor policy and keeps normal DIPs.
  ShowWindow(h.presenter->hwnd(), SW_MAXIMIZE); h.Pump(); h.Publish();
  local.ObserveEnvironment(missing, UiEnvironmentReason::kSystemNotification);
  h.Publish(); h.Pump(); h.Publish();
  LOCAL_CHECK(IsZoomed(h.presenter->hwnd()));
  LOCAL_CHECK(GetWinWindowMonitorKey(h.presenter->hwnd()) == primary->key);
  LOCAL_CHECK(local.PlacementFor(window_id)->frame_dip == preferred.frame_dip);
  local.ObserveEnvironment(actual, UiEnvironmentReason::kSystemNotification);
  h.Publish(); h.Pump(); h.Publish();
  LOCAL_CHECK(IsZoomed(h.presenter->hwnd()));
  LOCAL_CHECK(GetWinWindowMonitorKey(h.presenter->hwnd()) == secondary->key);
  LOCAL_CHECK(local.PlacementFor(window_id)->frame_dip == preferred.frame_dip);
  ShowWindow(h.presenter->hwnd(), SW_RESTORE); h.Pump(); h.Publish();

  ShowWindow(h.presenter->hwnd(), SW_MINIMIZE);
  local.ObserveEnvironment(missing, UiEnvironmentReason::kSystemNotification);
  h.Publish(); h.Pump();
  LOCAL_CHECK(IsIconic(h.presenter->hwnd()));
  ShowWindow(h.presenter->hwnd(), SW_RESTORE);
  // Restore applies the pending revision synchronously, without another model
  // publication or unrelated native notification to wake it up.
  LOCAL_CHECK(GetWinWindowMonitorKey(h.presenter->hwnd()) == primary->key);
  h.Pump(); h.Publish();
  LOCAL_CHECK(local.PlacementFor(window_id)->frame_dip == preferred.frame_dip);
  local.ObserveEnvironment(actual, UiEnvironmentReason::kSystemNotification);
  h.Publish(); h.Pump(); h.Publish();
  LOCAL_CHECK(GetWinWindowMonitorKey(h.presenter->hwnd()) == secondary->key);

  local.ObserveEnvironment(missing, UiEnvironmentReason::kSystemNotification);
  auto stale_fallback = h.Capture();
  auto const hwnd = h.presenter->hwnd();
  SendMessageW(hwnd, WM_ENTERSIZEMOVE, 0, 0);
  int const user_x = primary->work_area.x + 70;
  int const user_y = primary->work_area.y + 80;
  LOCAL_CHECK(SetWindowPos(hwnd, nullptr, user_x, user_y, 0, 0,
                          SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
  h.Apply(stale_fallback); h.CheckNative(user_x, user_y);
  SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);
  // The same old revision remains in the mirror before model acknowledgement.
  h.Apply(stale_fallback); h.CheckNative(user_x, user_y);
  h.Pump(); h.Publish(); h.CheckNative(user_x, user_y);
  LOCAL_CHECK(local.PlacementFor(window_id)->preferred_monitor == primary->key);
  local.ObserveEnvironment(actual, UiEnvironmentReason::kSystemNotification);
  h.Publish(); h.Pump(); h.Publish(); h.CheckNative(user_x, user_y);
  // Returning old monitor must respect the user's newer destination.
  LOCAL_CHECK(local.PlacementFor(window_id)->preferred_monitor == primary->key);
  h.ColdRestore(); h.CheckNative(user_x, user_y);
  h.CheckPortableInitial();
  {
    NativeLocalWindowHarness requested;
    auto& requested_local = *requested.model->window->device_state;
    requested.Drag(secondary->work_area.x + 40, secondary->work_area.y + 50, 400, 300);
    requested.Pump(); requested.Publish();
    requested_local.ObserveEnvironment(missing, UiEnvironmentReason::kSystemNotification);
    requested.Publish(); requested.Pump(); requested.Publish();
    SendMessageW(requested.presenter->hwnd(), WM_ENTERSIZEMOVE, 0, 0);
    requested_local.ObserveEnvironment(actual, UiEnvironmentReason::kSystemNotification);
    int const request_x = primary->work_area.x + 90;
    int const request_y = primary->work_area.y + 100;
    requested.model->window->RequestFrame(request_x, request_y, 420, 310);
    requested.Publish();
    SendMessageW(requested.presenter->hwnd(), WM_EXITSIZEMOVE, 0, 0);
    requested.CheckNative(request_x, request_y);
    requested.CheckNativeSize(420, 310);
    requested.Pump(); requested.Publish(); requested.CheckNative(request_x, request_y);
    LOCAL_CHECK(requested_local.PlacementFor(requested.model->window.id())->preferred_monitor == primary->key);
  }
  std::cout << "presenter_win_monitor_placement_e2e_test OK\n";
}
