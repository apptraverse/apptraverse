#ifndef APPTRAVERSE_EXAMPLE_SURFACES_WINDOW_CATALOG_ANDROID_LIFECYCLE_H_
#define APPTRAVERSE_EXAMPLE_SURFACES_WINDOW_CATALOG_ANDROID_LIFECYCLE_H_

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/ui/ui_device_state.h"

#include "window_catalog_android_embedded_profile.h"

namespace apptraverse::example::surfaces {

int RunWindowCatalogAndroidModel(WindowCatalogAndroidEmbeddedProfile& profile,
                                 ApplicationModelRuntime& runtime,
                                 ApplicationPublishedCallback on_published,
                                 ui::UiDeviceEnvironment const& startup_environment);

}  // namespace apptraverse::example::surfaces

#endif  // APPTRAVERSE_EXAMPLE_SURFACES_WINDOW_CATALOG_ANDROID_LIFECYCLE_H_
