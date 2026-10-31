#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_INITIAL_PUBLICATION_PROBE_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_INITIAL_PUBLICATION_PROBE_H_

#include <cstdint>
#include <filesystem>

namespace apptraverse::example::minimal {

struct AndroidInitialPublicationLabelProbeResult {
  bool ok{false};
  char stage[64]{};
  char detail[8192]{};
};

AndroidInitialPublicationLabelProbeResult RunAndroidInitialPublicationLabelProbe(
    std::filesystem::path const& profile_dir);

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_INITIAL_PUBLICATION_PROBE_H_
