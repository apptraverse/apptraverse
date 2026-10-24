#include <cstdio>
#include <cstdlib>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/distill.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/surfaces/surfaces_ids.h"
#include "apptraverse/surfaces/surfaces_model.h"
#include "apptraverse/surfaces/surfaces_registration.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "embedded_distilled_surfaces_supply.h"
#include "surfaces_embedded_release.h"

namespace {

void WriteLine(char const* key, char const* value) {
  std::printf("%s=%s\n", key, value);
}

void WriteUnsignedLine(char const* key, unsigned value) {
  std::printf("%s=%u\n", key, value);
}

}  // namespace

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSurfacesRegistration();

  ae::RamDomainStorage backing;
  apptraverse::InstallEmbeddedDistilledBacking(
      backing, apptraverse::embedded_distilled::surfaces::kEmbeddedGraph);
  std::set<ae::ObjId> const supply_ids =
      apptraverse::EmbeddedSupplyConstantIdSet(
          apptraverse::embedded_distilled::surfaces::kEmbeddedGraph);

  ae::RamDomainStorage user_storage;
  apptraverse::OverlayDomainStorage overlay{user_storage, backing,
                                            std::move(supply_ids)};
  ae::Domain domain{overlay};

  ae::ObjId const root_id{
      apptraverse::surfaces_demo::ToObjId(
          apptraverse::surfaces_demo::ObjId::Application)};
  auto application =
      apptraverse::LoadApplication<apptraverse::Application>(domain, root_id);

  WriteUnsignedLine("EMBEDDED_RELEASE",
                    apptraverse::surfaces_demo::kEmbeddedRelease);
  WriteUnsignedLine("SURFACE_COUNT",
                    static_cast<unsigned>(
                        application->surfaces->surfaces.size()));
  if (application->surfaces->surfaces.empty()) {
    return 1;
  }
  WriteUnsignedLine(
      "SURFACE_NUMBER",
      static_cast<unsigned>(application->surfaces->surfaces[0]->number));
  WriteLine("LABEL_ADD_EN", application->label_add->Resolve(0).c_str());
  WriteUnsignedLine("SUPPLY_CONSTANT_COUNT",
                    static_cast<unsigned>(supply_ids.size()));
  return 0;
}
