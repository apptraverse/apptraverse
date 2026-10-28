#ifndef APPTRAVERSE_DISTILLED_DOMAIN_EXPORT_H_
#define APPTRAVERSE_DISTILLED_DOMAIN_EXPORT_H_

#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/embedded_distilled_domain.h"

namespace apptraverse {

std::set<ae::ObjId> CollectReachableObjectIds(ae::Obj& root);

std::set<ae::ObjId> CollectStoredObjectIds(ae::RamDomainStorage const& storage);

bool LoadDirectoryDomainStorageIntoRam(std::filesystem::path const& root,
                                       ae::RamDomainStorage& out);

bool WriteEmbeddedDistilledSupplyHeader(
    std::filesystem::path const& output_path, std::string_view graph_namespace,
    std::string_view header_guard, ae::ObjId root_id,
    std::vector<ae::ObjId> const& supply_constant_ids,
    ae::RamDomainStorage const& storage, std::set<ae::ObjId> const& object_filter);

bool WriteEmbeddedDistilledGraphToDirectory(
    std::filesystem::path const& root, EmbeddedDistilledGraph const& graph);

}  // namespace apptraverse

#endif  // APPTRAVERSE_DISTILLED_DOMAIN_EXPORT_H_
