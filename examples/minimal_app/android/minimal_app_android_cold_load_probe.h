#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_COLD_LOAD_PROBE_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_COLD_LOAD_PROBE_H_

#include <cstdint>
#include <filesystem>

namespace apptraverse::example::minimal {

struct AndroidColdLoadLabelProbeResult {
  bool ok{false};
  std::uint64_t built_label_obj_id{0};
  std::uint64_t cold_label_obj_id{0};
  bool apply_label_valid{false};
  bool opener_label_valid{false};
  char detail[512]{};
};

AndroidColdLoadLabelProbeResult RunAndroidColdLoadLabelProbe(
    std::filesystem::path const& profile_dir);

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_COLD_LOAD_PROBE_H_
