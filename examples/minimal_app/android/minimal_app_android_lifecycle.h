#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_LIFECYCLE_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_LIFECYCLE_H_

#include "apptraverse/application_model_runtime.h"
#include "aether-objects/domain_storage/ram_domain_storage.h"
#include <filesystem>
#include "apptraverse/ui/ui_device_state.h"

namespace apptraverse::example::minimal {

// Shared embedded model session over independent portable USER and device LOCAL.
// Android lifecycle saves one native user.bin per partition, keeping live work
// RAM-backed. Tests without an Android Activity may omit the startup snapshot.
int RunMinimalApplicationModel(ae::RamDomainStorage& user_storage,
                               ae::RamDomainStorage& local_storage,
                               std::filesystem::path const& state_dir,
                               ApplicationModelRuntime& runtime,
                               ApplicationPublishedCallback on_published,
                               ui::UiDeviceEnvironment const* startup_environment = nullptr);
int PersistAndroidMinimalUser(ae::Domain& domain, ae::RamDomainStorage& user_storage,
                               ae::RamDomainStorage& local_storage,
                              std::filesystem::path const& state_dir);

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_LIFECYCLE_H_
