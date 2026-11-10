#include "apptraverse/embedded_distilled_domain.h"

namespace apptraverse {

void InstallEmbeddedDistilledBacking(ae::RamDomainStorage& backing,
                                     EmbeddedDistilledGraph const& graph) {
  for (std::size_t i = 0; i < graph.object_scope_count; ++i) {
    auto const& record = graph.object_scopes[i];
    backing.object_scopes[ae::ObjId{record.object_id}] = ae::ObjectScope{
        ae::ObjId{record.scope_root_id}, ae::ObjId{record.parent_scope_root_id},
        record.is_constant};
  }
  // The generated constant list remains compatible with older fixture headers.
  for (std::size_t i = 0; i < graph.supply_constant_ids.count; ++i) {
    backing.object_scopes[ae::ObjId{graph.supply_constant_ids.ids[i]}].is_constant = true;
  }
  for (std::size_t i = 0; i < graph.layer_count; ++i) {
    EmbeddedDistilledLayerRecord const& layer = graph.layers[i];
    ae::DomainQuery const query{ae::ObjId{layer.object_id}, layer.class_id,
                                layer.version};
    ae::ObjectData bytes;
    bytes.insert(bytes.end(), layer.bytes, layer.bytes + layer.size);
    backing.SaveData(query, std::move(bytes));
  }
}

std::set<ae::ObjId> EmbeddedSupplyConstantIdSet(
    EmbeddedDistilledGraph const& graph) {
  std::set<ae::ObjId> ids;
  for (std::size_t i = 0; i < graph.supply_constant_ids.count; ++i) {
    ids.insert(ae::ObjId{graph.supply_constant_ids.ids[i]});
  }
  return ids;
}

}  // namespace apptraverse
