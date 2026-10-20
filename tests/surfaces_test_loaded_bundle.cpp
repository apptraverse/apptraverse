#include "surfaces_test_loaded_bundle.h"

#include <cstdlib>
#include <iostream>

#include "apptraverse/application/model_checkpoint_persistence.h"
#include "apptraverse/surfaces/surfaces_ids.h"

#include "surfaces_runtime_execution.h"

namespace apptraverse::test {
namespace {

#define SURFACES_BUNDLE_CHECK(cond)                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"         \
                << __LINE__ << '\n';                                           \
      std::exit(1);                                                            \
    }                                                                          \
  } while (false)

}  // namespace

LoadedApplicationBundle::LoadedApplicationBundle() : domain{storage} {}

LoadedApplicationBundle::~LoadedApplicationBundle() {
  application = {};
}

void LoadedApplicationBundle::VerifyOwnerInvariants() const {
  SURFACES_BUNDLE_CHECK(application.is_valid());
  SURFACES_BUNDLE_CHECK(application.is_loaded());
  SURFACES_BUNDLE_CHECK(application->domain == &domain);
  SURFACES_BUNDLE_CHECK(&application->domain->domain_storage() == &storage);
  SURFACES_BUNDLE_CHECK(application->surfaces.is_valid());
  SURFACES_BUNDLE_CHECK(application->surfaces.is_loaded());
  SURFACES_BUNDLE_CHECK(application->surfaces->domain == &domain);
  for (Surface::ptr const& live : application->surfaces->surfaces) {
    SURFACES_BUNDLE_CHECK(live.is_valid() && live.is_loaded());
    SURFACES_BUNDLE_CHECK(live->domain == &domain);
  }
}

std::unique_ptr<LoadedApplicationBundle>
LoadedApplicationBundle::LoadFromModelCheckpoint(
    std::filesystem::path const& state_dir) {
  auto bundle = std::make_unique<LoadedApplicationBundle>();
  ModelCheckpointPaths const paths =
      ModelCheckpointPathsForStateDir(state_dir);
  bundle->application = Application::ptr::Create(
      ae::CreateWith{bundle->domain}.with_id(ae::ObjId{
          surfaces_demo::ToObjId(surfaces_demo::ObjId::Application)}));
  SURFACES_BUNDLE_CHECK(LoadModelCheckpoint(paths, bundle->domain,
                                            bundle->storage,
                                            *bundle->application));
  WireSurfacesDemoRuntimeExecution(bundle->app());
  bundle->VerifyOwnerInvariants();
  return bundle;
}

}  // namespace apptraverse::test
