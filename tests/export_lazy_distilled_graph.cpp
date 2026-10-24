#include <cstdlib>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/distilled_domain_export.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "distilled_lazy_graph_bootstrap.h"

namespace {

constexpr char kHeaderGuard[] =
    "APPTRAVERSE_EMBEDDED_DISTILLED_LAZY_DOC_SUPPLY_H_";
constexpr char kGraphNamespace[] = "lazy_doc";

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: export_lazy_distilled_graph <output.h>\n";
    return 2;
  }

  apptraverse::EnsureObjectRegistration();
  apptraverse::test::EnsureLazyDistilledGraphRegistration();

  ae::RamDomainStorage storage;
  ae::RamDomainStorage export_storage;
  ae::ObjId const root_id = apptraverse::test::DistillLazyDocGraphIntoStorage(
      storage, &export_storage);
  std::set<ae::ObjId> const object_filter =
      apptraverse::CollectStoredObjectIds(export_storage);

  if (!apptraverse::WriteEmbeddedDistilledSupplyHeader(
          argv[1], kGraphNamespace, kHeaderGuard, root_id, {}, export_storage,
          object_filter)) {
    std::cerr << "failed to write embedded lazy supply header\n";
    return 1;
  }
  return 0;
}
