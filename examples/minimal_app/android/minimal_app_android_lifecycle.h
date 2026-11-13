#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_LIFECYCLE_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_LIFECYCLE_H_

#include "apptraverse/application_model_runtime.h"
#include "aether-objects/domain_storage/ram_domain_storage.h"
#include <filesystem>

namespace apptraverse::example::minimal {

// One model session over the build-time embedded BACKING and |user_storage|
// as RAM USER. Shutdown and lifecycle checkpoints serialize that same USER
// to one user.bin file; the live model never reads or writes per-object files.
int RunMinimalApplicationModel(ae::RamDomainStorage& user_storage,
                               std::filesystem::path const& state_dir,
                               ApplicationModelRuntime& runtime,
                               ApplicationPublishedCallback on_published);
int PersistAndroidMinimalUser(ae::Domain& domain, ae::RamDomainStorage& user_storage,
                              std::filesystem::path const& state_dir);

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_LIFECYCLE_H_
