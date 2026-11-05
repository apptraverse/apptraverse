#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_LIFECYCLE_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_LIFECYCLE_H_

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/directory_domain_storage.h"

namespace apptraverse::example::minimal {

// One model session over the build-time embedded BACKING and |user_storage|
// as USER. Shutdown persist and background checkpoints
// (PersistMinimalApplicationEmbeddedUserOverlayFromDomain) write USER through
// the session overlay, so both use the same |user_storage|.
int RunMinimalApplicationModel(DirectoryDomainStorage& user_storage,
                               ApplicationModelRuntime& runtime,
                               ApplicationPublishedCallback on_published);

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_LIFECYCLE_H_
