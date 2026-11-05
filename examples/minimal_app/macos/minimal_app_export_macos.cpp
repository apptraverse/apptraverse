#include <filesystem>
#include <functional>
#include <set>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/distilled_domain_export.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/ui/ui_presenters.h"
#include "minimal_app_graph_macos.h"

int main(int argc, char** argv) {
  if (argc != 2) {
    return 2;
  }

  apptraverse::EnsureObjectRegistration();
  apptraverse::ui::EnsureUiPresenterRegistration();
  apptraverse::example::minimal::EnsureMinimalApplicationRegistration();

  std::hash<std::string> const hash;
  auto export_dir =
      std::filesystem::temp_directory_path() /
      ("apptraverse_export_minimal_macos_" +
       std::to_string(hash(std::filesystem::absolute(argv[1]).string())));
  std::error_code ignored;
  std::filesystem::remove_all(export_dir, ignored);

  apptraverse::DirectoryDomainStorage storage{export_dir};
  ae::Domain domain{storage};
  auto application =
      apptraverse::example::minimal::BuildMacMinimalApplicationGraph(domain);
  apptraverse::FinalizeDistilledGraph(*application);
  apptraverse::SaveDistilledRoot(*application);

  ae::RamDomainStorage export_storage;
  bool const loaded =
      apptraverse::LoadDirectoryDomainStorageIntoRam(export_dir, export_storage);
  auto const object_filter =
      apptraverse::CollectStoredObjectIds(export_storage);
  std::filesystem::remove_all(export_dir, ignored);
  return loaded &&
                 apptraverse::WriteEmbeddedDistilledSupplyHeader(
                     argv[1], "minimal",
                     "APPTRAVERSE_EMBEDDED_DISTILLED_MINIMAL_APP_MACOS_SUPPLY_H_",
                     apptraverse::example::minimal::MinimalApplicationRootId(),
                     {}, export_storage, object_filter)
             ? 0
             : 1;
}
