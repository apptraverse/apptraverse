#ifndef APPTRAVERSE_EXAMPLES_MINIMAL_APP_IOS_LIFECYCLE_H_
#define APPTRAVERSE_EXAMPLES_MINIMAL_APP_IOS_LIFECYCLE_H_

#include <filesystem>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "apptraverse/application_model_runtime.h"
#include "apptraverse/ui/ui_device_state.h"

namespace apptraverse::example::minimal {

class MinimalApplication;

int PersistIosMinimalApplication(MinimalApplication& application,
    ae::RamDomainStorage& user, ae::RamDomainStorage& local,
    std::filesystem::path const& state_dir);

int RunIosMinimalApplicationModel(
    std::filesystem::path const& state_dir, ae::RamDomainStorage& user,
    ae::RamDomainStorage& local, ApplicationModelRuntime& runtime,
    ApplicationPublishedCallback on_published,
    ui::UiDeviceEnvironment const& environment);

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLES_MINIMAL_APP_IOS_LIFECYCLE_H_
