#include <cstdlib>
#include <iostream>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/distill.h"
#include "apptraverse/distilled_domain_export.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_presenters.h"

#include "minimal_app_graph.h"
#include "minimal_application.h"

namespace {

constexpr char kHeaderGuard[] =
    "APPTRAVERSE_EMBEDDED_DISTILLED_MINIMAL_APP_SUPPLY_H_";
constexpr char kGraphNamespace[] = "minimal";

void PersistDistilledMinimalGraph(
    ae::Domain& domain,
    apptraverse::example::minimal::MinimalApplication::ptr application) {
  ae::Ptr<apptraverse::example::minimal::MinimalApplication> root =
      application.Load();
  apptraverse::SaveDistilledRoot(*root);
  std::vector<apptraverse::Node*> nodes;
  apptraverse::CollectReachableNodes(*root, nodes);
  for (apptraverse::Node* node : nodes) {
    if (node->base.is_valid()) {
      node->base.Save();
    }
  }
  root->main_window.Save();
  if (root->main_window.is_valid()) {
    ae::Ptr<apptraverse::ui::UiDesktopWindow> window =
        root->main_window.Load();
    if (window->presenter.is_valid()) {
      window->presenter.Save();
    }
  }
  ae::DomainGraph graph{&domain};
  (void)graph.SaveRoot(root, apptraverse::example::minimal::MinimalApplicationRootId());
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: apptraverse_export_minimal_distilled_graph "
                 "<output.h>\n";
    return 2;
  }

  apptraverse::EnsureObjectRegistration();
  apptraverse::ui::EnsureUiPresenterRegistration();
  apptraverse::ui::windows::EnsureWinUiPresenterRegistration();
  apptraverse::example::minimal::EnsureMinimalApplicationRegistration();

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application =
      apptraverse::example::minimal::BuildMinimalApplicationGraph(domain);
  ae::Ptr<apptraverse::example::minimal::MinimalApplication> root =
      application.Load();
  apptraverse::FinalizeDistilledGraph(*root);
  apptraverse::StabilizeDistilledPersistentSnapshot(*root);
  PersistDistilledMinimalGraph(domain, application);

  std::set<ae::ObjId> const object_filter =
      apptraverse::CollectStoredObjectIds(storage);
  if (!apptraverse::WriteEmbeddedDistilledSupplyHeader(
          argv[1], kGraphNamespace, kHeaderGuard,
          apptraverse::example::minimal::MinimalApplicationRootId(), {},
          storage, object_filter)) {
    std::cerr << "failed to write embedded minimal app supply header\n";
    return 1;
  }
  return 0;
}
