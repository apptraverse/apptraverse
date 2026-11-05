#ifndef APPTRAVERSE_EXAMPLES_MINIMAL_APP_MACOS_LIFECYCLE_H_
#define APPTRAVERSE_EXAMPLES_MINIMAL_APP_MACOS_LIFECYCLE_H_

#include <filesystem>

#include "apptraverse/application_model_runtime.h"

namespace apptraverse::example::minimal {

int RunMacMinimalApplicationModel(
    std::filesystem::path const& state_dir, ApplicationModelRuntime& runtime,
    ApplicationPublishedCallback on_published);

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLES_MINIMAL_APP_MACOS_LIFECYCLE_H_
