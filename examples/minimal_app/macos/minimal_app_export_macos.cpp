#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/distilled_domain_export.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/macos/macos_ui_presenters.h"
#include "apptraverse/ui/ui_presenters.h"
#include "minimal_app_graph_macos.h"

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  apptraverse::EnsureObjectRegistration();
  apptraverse::ui::EnsureUiPresenterRegistration();
  apptraverse::ui::macos::EnsureMacUiPresenterRegistration();
  apptraverse::example::minimal::EnsureMinimalApplicationRegistration();

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application =
      apptraverse::example::minimal::BuildMacMinimalApplicationGraph(domain);
  apptraverse::FinalizeDistilledGraph(*application);
  apptraverse::SaveDistilledRoot(*application);
  return apptraverse::WriteEmbeddedDistilledSupplyHeader(
      argv[1], "minimal",
      "APPTRAVERSE_EMBEDDED_DISTILLED_MINIMAL_APP_MACOS_SUPPLY_H_",
      apptraverse::example::minimal::MinimalApplicationRootId(), {}, storage,
      apptraverse::CollectStoredObjectIds(storage)) ? 0 : 1;
}
