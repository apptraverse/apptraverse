#include "window_catalog_android_embedded_profile.h"

#include <cstdlib>
#include "aether-objects/obj/obj_id.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/single_file_user_storage.h"
#include "android_log.h"

namespace apptraverse::example::surfaces {
WindowCatalogAndroidEmbeddedProfile::WindowCatalogAndroidEmbeddedProfile(
    std::filesystem::path const& user_dir) : state_dir{user_dir} {
#if defined(APPTRAVERSE_WINDOW_CATALOG_DEBUG_FRESH)
  std::error_code error;
  std::filesystem::create_directories(user_dir, error);
  if (error) {
    WriteFatalStderr("fatal: cannot create Android development directory\n");
    std::abort();
  }
  for (;;) {
    auto const run = user_dir /
        ("debug-run-" + std::to_string(ae::ObjId::GenerateUnique().id()));
    if (std::filesystem::create_directory(run, error)) {
      state_dir = run / "user";
      break;
    }
    if (error) {
      WriteFatalStderr("fatal: cannot create Android Debug profile\n");
      std::abort();
    }
  }
  android::LogMarker("WINDOW_CATALOG_DEBUG_FRESH_PROFILE=" + state_dir.string());
#else
  if (!LoadUserStorageFile(user, state_dir)) {
    WriteFatalStderr("fatal: cannot load Android USER file\n");
    std::abort();
  }
#endif
}
}  // namespace apptraverse::example::surfaces
