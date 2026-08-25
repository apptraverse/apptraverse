#ifndef APPTRAVERSE_DEMO_BOOTSTRAP_H_
#define APPTRAVERSE_DEMO_BOOTSTRAP_H_

#include <filesystem>
#include <memory>

#include "aether/clock.h"
#include "aether/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "demo_model.h"

namespace apptraverse {

struct DemoRuntime {
  std::unique_ptr<DirectoryDomainStorage> storage;
  std::unique_ptr<ae::Domain> model_domain;
  std::unique_ptr<ae::Domain> ui_domain;
  Application::ptr application;
};

Application::ptr BuildDemoGraph(ae::Domain& domain);
void DistillModel(std::filesystem::path const& dir);
DemoRuntime LoadDemoModel(std::filesystem::path const& dir);

}  // namespace apptraverse

#endif  // APPTRAVERSE_DEMO_BOOTSTRAP_H_
