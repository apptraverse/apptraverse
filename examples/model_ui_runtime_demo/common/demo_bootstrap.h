#ifndef APPTRAVERSE_DEMO_BOOTSTRAP_H_
#define APPTRAVERSE_DEMO_BOOTSTRAP_H_

#include <filesystem>
#include <memory>

#include "aether/clock.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/overlay_domain_storage.h"
#include "demo_ids.h"
#include "demo_model.h"

namespace apptraverse {

// Member order: storages before domains before strong roots so destruction is
// ui_application -> application -> ui_domain -> model_domain -> ui_storage ->
// model_storage.
struct DemoRuntime {
  std::unique_ptr<DirectoryDomainStorage> storage;
  std::unique_ptr<OverlayDomainStorage> ui_storage;
  std::unique_ptr<ae::Domain> model_domain;
  std::unique_ptr<ae::Domain> ui_domain;
  Application::ptr application;
  Application::ptr ui_application;
};

Application::ptr BuildDemoGraph(ae::Domain& domain);
void DistillModel(std::filesystem::path const& dir);
DemoRuntime LoadDemoModel(std::filesystem::path const& dir);

}  // namespace apptraverse

#endif  // APPTRAVERSE_DEMO_BOOTSTRAP_H_
