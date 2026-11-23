#include "apptraverse/embedded_distilled_domain.h"

#include <cstdlib>

#include "object_storage_compression.h"

namespace apptraverse {

bool DecodeEmbeddedDistilledLayer(EmbeddedDistilledLayerRecord const& layer,
                                 ae::ObjectData& bytes) {
  if (layer.expanded_size == 0) {
    bytes.clear();
    if (layer.size != 0) bytes.assign(layer.bytes, layer.bytes + layer.size);
    return true;
  }
  ae::ObjectData decoded(layer.expanded_size);
  if (!storage_compression::ExpandEmbedded({layer.bytes, layer.size}, decoded))
    return false;
  bytes = std::move(decoded);
  return true;
}

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
    // Generated supplies and their consumers share one build configuration.
    if (!DecodeEmbeddedDistilledLayer(layer, bytes)) std::abort();
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
