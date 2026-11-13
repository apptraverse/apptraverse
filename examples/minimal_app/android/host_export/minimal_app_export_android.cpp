#include <filesystem>
#include <iostream>
#include <set>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/distill.h"
#include "apptraverse/distilled_domain_export.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/android/android_ui_registration.h"
#include "apptraverse/ui/ui_presenters.h"

#include "minimal_app_android_graph.h"
#include "minimal_application.h"

namespace {

constexpr char kHeaderGuard[] =
    "APPTRAVERSE_EMBEDDED_DISTILLED_MINIMAL_APP_SUPPLY_H_";
constexpr char kGraphNamespace[] = "minimal";

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: apptraverse_export_minimal_distilled_graph_android "
                 "<output.h>\n";
    return 2;
  }

  apptraverse::EnsureObjectRegistration();
  apptraverse::ui::EnsureUiPresenterRegistration();
  apptraverse::ui::android::EnsureAndroidUiPresenterRegistration();
  apptraverse::example::minimal::EnsureMinimalApplicationRegistration();

  ae::RamDomainStorage export_storage;
  ae::Domain domain{export_storage};
  auto application = apptraverse::example::minimal::BuildAndroidMinimalApplicationGraph(domain);
  apptraverse::FinalizeDistilledGraph(*application);
  apptraverse::SaveDistilledRoot(*application);
  std::set<ae::ObjId> const object_filter =
      apptraverse::CollectStoredObjectIds(export_storage);

  if (!apptraverse::WriteEmbeddedDistilledSupplyHeader(
          argv[1], kGraphNamespace, kHeaderGuard,
          apptraverse::example::minimal::MinimalApplicationRootId(), {},
          export_storage, object_filter)) {
    std::cerr << "failed to write embedded minimal app supply header\n";
    return 1;
  }
  return 0;
}
