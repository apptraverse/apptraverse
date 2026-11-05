#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_EMBEDDED_PROFILE_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_EMBEDDED_PROFILE_H_

#include <filesystem>

#include "aether-objects/domain_storage/ram_domain_storage.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/overlay_domain_storage.h"

namespace apptraverse::example::minimal {

// The storage RunMinimalApplicationModel loads from: the build-time embedded
// supply as BACKING and |user_dir| as USER, composed by the standard overlay.
struct MinimalAppEmbeddedProfile {
  explicit MinimalAppEmbeddedProfile(std::filesystem::path const& user_dir);

  DirectoryDomainStorage user;
  ae::RamDomainStorage backing;
  OverlayDomainStorage overlay;
};

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_EMBEDDED_PROFILE_H_
