#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_COLD_RELOAD_APPLY_PROBE_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_COLD_RELOAD_APPLY_PROBE_H_

#include <cstdint>
#include <filesystem>

namespace apptraverse::example::minimal {

struct AndroidColdReloadApplyProbeResult {
  bool ok{false};
  int selection_index{-999};
  bool apply_enabled{true};
  char title[64]{};
  char detail[128]{};
};

AndroidColdReloadApplyProbeResult RunAndroidColdReloadApplyStateProbe(
    std::filesystem::path const& profile_dir);

}  // namespace apptraverse::example::minimal

#endif
