#ifndef APPTRAVERSE_EXAMPLE_SURFACES_WINDOW_CATALOG_ANDROID_EMBEDDED_PROFILE_H_
#define APPTRAVERSE_EXAMPLE_SURFACES_WINDOW_CATALOG_ANDROID_EMBEDDED_PROFILE_H_

#include <filesystem>

#include "aether-objects/domain_storage/ram_domain_storage.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/overlay_domain_storage.h"

namespace apptraverse::example::surfaces {

struct WindowCatalogAndroidEmbeddedProfile {
  explicit WindowCatalogAndroidEmbeddedProfile(
      std::filesystem::path const& user_dir);

  DirectoryDomainStorage user;
  ae::RamDomainStorage backing;
  OverlayDomainStorage overlay;
};

}  // namespace apptraverse::example::surfaces

#endif  // APPTRAVERSE_EXAMPLE_SURFACES_WINDOW_CATALOG_ANDROID_EMBEDDED_PROFILE_H_
