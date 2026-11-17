#if !defined(_WIN32)
int main() { return 0; }
#else
#include "window_local_environment_fixture.h"
int main() {
  using namespace apptraverse::test;
  NativeLocalWindowHarness h;
  auto const generation = h.model->window->Generation();
  for (int i = 0; i != 50; ++i) h.Drag(50 + i, 60 + i);
  // Native observations are accepted by LOCAL, independently of publication.
  h.CheckPortableInitial(); h.CheckNative(99, 109);
  h.Pump(); h.Publish(); h.CheckNative(99, 109);
  h.CheckPortableInitial(); LOCAL_CHECK(h.model->window->Generation() == generation);
  auto const& local = *h.model->window->device_state;
  auto const* placement = local.PlacementFor(h.model->window.id());
  LOCAL_CHECK(placement != nullptr);
  LOCAL_CHECK(placement->preferred_monitor == local.windows.front().monitor_key);
  LOCAL_CHECK(local.windows.front().frame_dip == placement->frame_dip);
  h.Drag(150, 160); h.Pump(); auto stale = h.Capture();
  h.Drag(99, 109); h.Pump(); h.Apply(stale); h.CheckNative(99, 109);
  h.Publish(); h.CheckNative(99, 109);
  h.Drag(99, 109, 700, 520); h.Pump(); h.Publish();
  h.CheckNativeSize(700, 520); h.CheckPortableInitial();
  h.ColdRestore(); h.CheckNative(99, 109); h.CheckNativeSize(700, 520);
  std::cout << "presenter_win_desktop_interactive_drag_e2e_test OK\n";
}
#endif
