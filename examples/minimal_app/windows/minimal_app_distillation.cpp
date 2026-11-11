#include "minimal_app_distillation.h"

#include <cstdlib>
#include "minimal_app_graph.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/distilled_domain_export.h"

namespace apptraverse::example::minimal {
std::filesystem::path DistillMinimalApplication(std::filesystem::path const& output_directory) {
  auto const backing_directory = output_directory /
      ("initial-" + std::to_string(ae::ObjId::GenerateUnique().id()));
  DirectoryDomainStorage storage{backing_directory};
  ae::Domain domain{storage};
  auto application = BuildMinimalApplicationGraph(domain);
  FinalizeDistilledGraph(*application);
  SaveDistilledRoot(*application);
  ae::RamDomainStorage exported;
  bool const loaded = LoadDirectoryDomainStorageIntoRam(backing_directory, exported);
  if (!loaded) { std::abort(); }
  bool const written = WriteEmbeddedDistilledSupplyHeader(
      output_directory / "embedded_distilled_minimal_app_supply.h", "minimal",
      "APPTRAVERSE_EMBEDDED_DISTILLED_MINIMAL_APP_SUPPLY_H_",
      MinimalApplicationRootId(), {}, exported, CollectStoredObjectIds(exported));
  if (!written) { std::abort(); }
  return backing_directory;
}
}
