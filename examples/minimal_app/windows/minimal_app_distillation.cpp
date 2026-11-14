#include "minimal_app_distillation.h"

#include <cstdlib>
#include "minimal_app_graph.h"
#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/distilled_domain_export.h"

namespace apptraverse::example::minimal {
std::filesystem::path DistillMinimalApplication(std::filesystem::path const& output_directory) {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildMinimalApplicationGraph(domain);
  FinalizeDistilledGraph(*application);
  SaveDistilledRoot(*application);
  bool const written = WriteEmbeddedDistilledSupplyHeader(
      output_directory / "embedded_distilled_minimal_app_supply.h", "minimal",
      "APPTRAVERSE_EMBEDDED_DISTILLED_MINIMAL_APP_SUPPLY_H_",
      MinimalApplicationRootId(), {}, storage, CollectStoredObjectIds(storage));
  if (!written) { std::abort(); }
  return output_directory / "embedded_distilled_minimal_app_supply.h";
}
}
