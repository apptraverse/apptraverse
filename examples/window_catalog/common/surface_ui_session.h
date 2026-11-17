#ifndef APPTRAVERSE_EXAMPLES_SURFACE_UI_SESSION_H_
#define APPTRAVERSE_EXAMPLES_SURFACE_UI_SESSION_H_
#include <filesystem>
#include "apptraverse/application_model_runtime.h"
#include "apptraverse/ui/ui_device_state.h"
#include "surface_ui_model.h"
namespace apptraverse::example::surface_ui {
struct SurfaceSession {
  ApplicationModelRuntime runtime;
  SurfaceFactories factories;
  std::filesystem::path state_dir{"surface_generic_s0_state"};
  std::filesystem::path local_seed_dir;
  ui::UiDeviceEnvironment environment;
  ae::ObjId pending_focus_for_ui;
  bool load_only = false; // Explicit matching-supply cold run in a Debug producer.
  int Run(ApplicationPublishedCallback);
};
std::set<ae::ObjId> SurfaceSupplyConstants(ae::RamDomainStorage const&);
void MakeSurfaceSupply(ae::RamDomainStorage&, SurfaceFactories, bool v1);
bool ExportSurfaceSupply(std::filesystem::path const&, SurfaceFactories, bool v1);
}
#endif
