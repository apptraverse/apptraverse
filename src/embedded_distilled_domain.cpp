#include "apptraverse/embedded_distilled_domain.h"

namespace apptraverse {

void InstallEmbeddedDistilledBacking(ae::RamDomainStorage& backing,
                                     EmbeddedDistilledGraph const& graph) {
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
