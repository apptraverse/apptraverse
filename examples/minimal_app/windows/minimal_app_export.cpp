#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/distilled_domain_export.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/ui/ui_presenters.h"

#include "minimal_app_graph.h"
#include "minimal_application.h"

namespace {

constexpr char kHeaderGuard[] =
    "APPTRAVERSE_EMBEDDED_DISTILLED_MINIMAL_APP_SUPPLY_H_";
constexpr char kGraphNamespace[] = "minimal";

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

  std::filesystem::path const export_dir =
      std::filesystem::temp_directory_path() /
      "apptraverse_export_minimal_distilled_graph";
  std::error_code remove_ec;
  std::filesystem::remove_all(export_dir, remove_ec);

  apptraverse::DirectoryDomainStorage storage{export_dir};
  ae::Domain domain{storage};
  auto application =
      apptraverse::example::minimal::BuildMinimalApplicationGraph(domain);
  apptraverse::FinalizeDistilledGraph(*application);
  apptraverse::SaveDistilledRoot(*application);

  ae::RamDomainStorage export_storage;
  if (!apptraverse::LoadDirectoryDomainStorageIntoRam(export_dir,
                                                      export_storage)) {
    std::cerr << "failed to load distilled minimal app layers from export dir\n";
    return 1;
  }
  std::set<ae::ObjId> const object_filter =
      apptraverse::CollectStoredObjectIds(export_storage);
  std::filesystem::remove_all(export_dir, remove_ec);

  if (!apptraverse::WriteEmbeddedDistilledSupplyHeader(
          argv[1], kGraphNamespace, kHeaderGuard,
          apptraverse::example::minimal::MinimalApplicationRootId(), {},
          export_storage, object_filter)) {
    std::cerr << "failed to write embedded minimal app supply header\n";
    return 1;
  }
  return 0;
}
