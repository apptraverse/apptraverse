#include "apptraverse/shared_network_graph.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "aether-miscpp/serialization/binary_archive.h"
#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/registry.h"
#include "apptraverse/event.h"
#include "apptraverse/node.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/operation_storage.h"

namespace apptraverse {

void BuildNetworkSharedScratch(ae::Obj const& root,
                               ae::RamDomainStorage& scratch) {
  EnsureObjectRegistration();
  assert(root.domain != nullptr);

  ae::Domain scratch_domain{scratch};
  ae::DomainGraph graph{&scratch_domain,
                        ae::GraphSerializationScope::NetworkShared};

  auto ptr = root.domain->Find(root.obj_id);
  assert(ptr);
  auto* factory = ae::Registry::GetRegistry().FindFactory(root.GetClassId());
  assert(factory != nullptr);
  assert(factory->save != nullptr);
  factory->save(&graph, ptr, root.obj_id);
}

namespace {

void TransferRamObject(ae::RamDomainStorage const& src, ae::ObjId obj_id,
                       ae::IDomainStorage& dst) {
  auto const it = src.state.find(obj_id);
  if (it == src.state.end() || !it->second.has_value()) {
    return;
  }
  for (auto const& [class_id, versions] : *it->second) {
    for (auto const& [version, data] : versions) {
      auto writer = dst.Store(ae::DomainQuery{obj_id, class_id, version});
      assert(writer != nullptr);
      if (!data.empty()) {
        auto const result = writer->Write(
            ae::seri::DataWriteTag{data.data(), data.size()});
        assert(result);
        (void)result;
      }
    }
  }
}

ae::ObjId AllocateUniqueReceiverObjId(
    ae::Domain const& domain,
    ae::IDomainStorage& storage,
    std::set<ae::ObjId> const& reserved_ids) {
  while (true) {
    auto const id = ae::ObjId::GenerateUnique();
    if (!id.is_valid()) {
      continue;
    }
    if (reserved_ids.find(id) != reserved_ids.end()) {
      continue;
    }
    if (domain.Find(id)) {
      continue;
    }
    if (!storage.Enumerate(id).empty()) {
      continue;
    }
    return id;
  }
}

}  // namespace

void CopyNetworkSharedObjectGraph(ae::Obj const& root,
                                  ae::IDomainStorage& target_storage) {
  ae::RamDomainStorage scratch;
  BuildNetworkSharedScratch(root, scratch);
  CommitObjectGraph(scratch, target_storage);
}

void CopySharedNetworkGraph(SharedNode::ptr source,
                            ae::Domain& target_domain,
                            ae::IDomainStorage& target_storage) {
  assert(source.is_valid());
  assert(source.is_loaded());
  (void)target_domain;
  CopyNetworkSharedObjectGraph(*source, target_storage);
}

bool SerializeObjectGraph(ae::RamDomainStorage const& storage,
                          std::vector<std::uint8_t>& out) {
  out.clear();
  ae::seri::BinaryVectorBuffer buffer{out};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  return archive.Save(storage.state).IsOk();
}

std::vector<std::uint8_t> SerializeObjectGraph(
    ae::RamDomainStorage const& storage) {
  std::vector<std::uint8_t> out;
  SerializeObjectGraph(storage, out);
  return out;
}

bool DeserializeObjectGraph(std::vector<std::uint8_t> const& payload,
                            ae::RamDomainStorage& storage) {
  storage.state.clear();
  std::vector<std::uint8_t> payload_copy = payload;
  ae::seri::BinaryVectorBuffer buffer{payload_copy};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  if (auto const res = archive.Load(storage.state); res.IsErr()) {
    return false;
  }
  return archive.buffer().read_position() == payload.size();
}

std::vector<std::uint8_t> SerializeNetworkSharedObjectGraph(
    ae::Obj const& root) {
  ae::RamDomainStorage scratch;
  BuildNetworkSharedScratch(root, scratch);
  return SerializeObjectGraph(scratch);
}

FrozenNodeState FreezeNetworkSharedNodeState(SharedNode const& root) {
  ae::RamDomainStorage scratch;
  BuildNetworkSharedScratch(root, scratch);

  std::vector<SharedEventId> covered_event_ids;
  {
    ae::Domain scratch_domain{scratch};
    ae::DomainGraph scratch_graph{&scratch_domain};
    auto loaded_root = scratch_graph.LoadRoot(root.obj_id);
    assert(loaded_root);
    auto const& scratch_shared_node =
        static_cast<SharedNode const&>(*loaded_root);
    for (auto const& record : scratch_shared_node.journal) {
      if (record.HasSharedIdentity()) {
        covered_event_ids.push_back(record.identity);
      }
    }
  }

  return FrozenNodeState{
      .payload = SerializeObjectGraph(scratch),
      .covered_event_ids = std::move(covered_event_ids),
  };
}

bool ValidateStoredClassChains(
    ae::RamDomainStorage const& parsed,
    std::vector<StoredClassChainInfo>* out_chains) {
  EnsureObjectRegistration();
  auto& registry = ae::Registry::GetRegistry();
  auto const obj_class_id = ae::Obj::kClassId;
  if (out_chains != nullptr) {
    out_chains->clear();
  }

  for (auto const& [obj_id, classes_opt] : parsed.state) {
    if (!classes_opt.has_value()) {
      continue;
    }
    if (!obj_id.is_valid()) {
      return false;
    }
    auto const& classes = *classes_opt;
    if (classes.empty()) {
      return false;
    }

    std::vector<std::uint32_t> chain;
    chain.reserve(classes.size());

    // Check that every registered class layer exists in registry and has no unknown layers.
    // Note: ae::Obj is the root of all objects in aether-objects, but ae::Registry does not
    // register an entry for ae::Obj itself (its base_id is "Obj").
    for (auto const& [class_id, versions] : classes) {
      if (class_id != obj_class_id && !registry.IsExisting(class_id)) {
        return false;
      }
      if (versions.empty()) {
        return false;
      }
      chain.push_back(class_id);
    }

    // Check that every pair of classes in chain is related (one inherits from the other).
    for (std::size_t i = 0; i < chain.size(); ++i) {
      for (std::size_t j = i + 1; j < chain.size(); ++j) {
        auto const c1 = chain[i];
        auto const c2 = chain[j];
        if (c1 == obj_class_id || c2 == obj_class_id) {
          // ae::Obj is the base of everything
          continue;
        }
        int const d12 = registry.GenerationDistance(c1, c2);
        int const d21 = registry.GenerationDistance(c2, c1);
        if (d12 < 0 && d21 < 0) {
          return false;
        }
      }
    }

    // Sort base -> derived: left comes before right if right is derived from left.
    // Strict weak ordering: when a == b, must return false (including when both are obj_class_id).
    std::sort(chain.begin(), chain.end(), [&registry, obj_class_id](auto a, auto b) {
      if (a == b) {
        return false;
      }
      if (a == obj_class_id) {
        return true;
      }
      if (b == obj_class_id) {
        return false;
      }
      int const dist = registry.GenerationDistance(a, b);
      return dist > 0;
    });

    auto const most_derived = chain.back();
    if (most_derived == obj_class_id) {
      return false;
    }

    // Verify most_derived has a registered factory with create, load, save.
    auto* factory = registry.FindFactory(most_derived);
    if (factory == nullptr || factory->create == nullptr ||
        factory->load == nullptr || factory->save == nullptr) {
      return false;
    }

    // Verify that every stored class in chain is an ancestor of most_derived
    // (or equal to it). Since we checked pairwise relatedness and sorted base->derived,
    // verify each element's distance to most_derived is >= 0.
    for (auto const c : chain) {
      if (c == obj_class_id) {
        continue;
      }
      if (registry.GenerationDistance(c, most_derived) < 0) {
        return false;
      }
    }

    if (out_chains != nullptr) {
      out_chains->push_back(StoredClassChainInfo{
          .obj_id = obj_id,
          .chain = chain,
          .most_derived_class_id = most_derived,
      });
    }
  }

  return true;
}

void CommitObjectGraph(ae::RamDomainStorage const& parsed,
                       ae::IDomainStorage& target_storage) {
  for (auto const& [obj_id, classes] : parsed.state) {
    if (!classes.has_value()) {
      continue;
    }
    TransferRamObject(parsed, obj_id, target_storage);
  }
}

bool ImportObjectGraphPayload(std::vector<std::uint8_t> const& payload,
                              ae::IDomainStorage& target_storage) {
  ae::RamDomainStorage parsed;
  if (!DeserializeObjectGraph(payload, parsed)) {
    return false;
  }
  CommitObjectGraph(parsed, target_storage);
  return true;
}

bool FreezeEventPayload(
    ae::Obj const& event,
    EventGraphExportBoundary const& boundary,
    std::vector<std::uint8_t>& out_payload) {
  EnsureObjectRegistration();
  assert(event.domain != nullptr);

  if (!boundary.IsPermitted(event.obj_id)) {
    return false;
  }

  ae::RamDomainStorage scratch;
  BuildNetworkSharedScratch(event, scratch);

  for (auto const& [obj_id, classes] : scratch.state) {
    if (!classes.has_value()) {
      continue;
    }
    if (!boundary.IsPermitted(obj_id)) {
      return false;
    }
  }

  if (!ValidateClosedEventGraphStorage(scratch, event.obj_id,
                                       event.GetClassId())) {
    return false;
  }

  out_payload.clear();
  ae::seri::BinaryVectorBuffer buffer{out_payload};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  if (auto const res = archive.Save(event.obj_id); res.IsErr()) {
    return false;
  }
  if (auto const res = archive.Save(scratch.state); res.IsErr()) {
    return false;
  }
  return true;
}

bool FreezeEventPayload(
    ae::Obj const& event,
    std::vector<std::uint8_t>& out_payload) {
  return FreezeEventPayload(event, EventGraphExportBoundary{event.obj_id},
                            out_payload);
}

bool ParseEventPayload(
    std::vector<std::uint8_t> const& payload,
    ae::RamDomainStorage& parsed,
    ae::ObjId& out_root_id) {
  parsed.state.clear();
  std::vector<std::uint8_t> payload_copy = payload;
  ae::seri::BinaryVectorBuffer buffer{payload_copy};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  if (auto const res = archive.Load(out_root_id); res.IsErr()) {
    return false;
  }
  if (!out_root_id.is_valid()) {
    return false;
  }
  if (auto const res = archive.Load(parsed.state); res.IsErr()) {
    return false;
  }
  if (archive.buffer().read_position() != payload.size()) {
    return false;
  }
  auto const it = parsed.state.find(out_root_id);
  if (it == parsed.state.end() || !it->second.has_value()) {
    return false;
  }
  return true;
}

bool ValidateClosedEventGraphStorage(
    ae::RamDomainStorage const& parsed,
    ae::ObjId root_event_id,
    std::uint32_t expected_event_class_id,
    std::vector<StoredClassChainInfo>* out_chains) {
  if (!root_event_id.is_valid()) {
    return false;
  }
  auto const root_it = parsed.state.find(root_event_id);
  if (root_it == parsed.state.end() || !root_it->second.has_value()) {
    return false;
  }

  std::vector<StoredClassChainInfo> chains;
  if (!ValidateStoredClassChains(parsed, &chains)) {
    return false;
  }

  auto const chain_it =
      std::find_if(chains.begin(), chains.end(),
                   [&](StoredClassChainInfo const& info) {
                     return info.obj_id == root_event_id;
                   });
  if (chain_it == chains.end()) {
    return false;
  }

  auto& reg = ae::Registry::GetRegistry();
  if (reg.GenerationDistance(Event::kClassId,
                             chain_it->most_derived_class_id) < 0) {
    return false;
  }
  if (expected_event_class_id != 0 &&
      chain_it->most_derived_class_id != expected_event_class_id) {
    return false;
  }

  ae::RamDomainStorage scratch_copy = parsed;
  ae::Domain scratch_domain{scratch_copy};
  ae::DomainGraph scratch_graph{&scratch_domain,
                                ae::GraphSerializationScope::NetworkShared};

  auto root_ptr = scratch_graph.LoadRoot(root_event_id);
  if (!root_ptr) {
    return false;
  }

  std::map<ae::ObjId, std::uint32_t> most_derived_classes;
  for (auto const& info : chains) {
    most_derived_classes[info.obj_id] = info.most_derived_class_id;
    auto obj = scratch_graph.LoadRoot(info.obj_id);
    if (!obj) {
      return false;
    }
  }

  bool validation_ok = true;
  detail::OperationStorage val_storage{
      detail::OperationMode::kValidate,
      &parsed,
      &most_derived_classes,
      /*target_domain=*/nullptr,
      /*real_storage=*/nullptr,
      /*old_to_new=*/nullptr,
      /*new_id_to_obj=*/nullptr,
      &validation_ok};

  ae::Domain val_domain{val_storage};
  ae::DomainGraph val_graph{&val_domain,
                            ae::GraphSerializationScope::NetworkShared};

  for (auto const& info : chains) {
    auto obj = scratch_domain.Find(info.obj_id);
    if (!obj) {
      return false;
    }
    val_graph.SaveRoot(obj, info.obj_id);
    if (!validation_ok) {
      return false;
    }
  }

  if (out_chains != nullptr) {
    *out_chains = std::move(chains);
  }
  return true;
}

ae::Ptr<Event> ImportClosedEventGraph(
    ae::RamDomainStorage const& parsed,
    ae::ObjId root_event_id,
    ae::Domain& receiver_domain,
    ae::IDomainStorage& receiver_storage,
    std::set<ae::ObjId>& reserved_ids,
    std::map<ae::ObjId, ae::ObjId>* out_mapping) {
  EnsureObjectRegistration();

  std::vector<StoredClassChainInfo> chains;
  if (!ValidateClosedEventGraphStorage(parsed, root_event_id, 0, &chains)) {
    return {};
  }

  std::map<ae::ObjId, ae::ObjId> old_to_new;
  for (auto const& info : chains) {
    auto const new_id = AllocateUniqueReceiverObjId(
        receiver_domain, receiver_storage, reserved_ids);
    reserved_ids.insert(new_id);
    old_to_new[info.obj_id] = new_id;
  }

  if (out_mapping != nullptr) {
    *out_mapping = old_to_new;
  }

  ae::RamDomainStorage staging_storage = parsed;
  ae::Domain staging_domain{staging_storage};
  ae::DomainGraph staging_graph{&staging_domain,
                                ae::GraphSerializationScope::NetworkShared};
  auto staging_root = staging_graph.LoadRoot(root_event_id);
  if (!staging_root) {
    return {};
  }

  for (auto const& info : chains) {
    auto obj = staging_graph.LoadRoot(info.obj_id);
    if (!obj) {
      return {};
    }
  }

  std::vector<std::pair<ae::ObjId, ae::Ptr<ae::Obj>>> loaded_objects;
  loaded_objects.reserve(chains.size());
  for (auto const& info : chains) {
    auto obj = staging_domain.Find(info.obj_id);
    assert(obj);
    loaded_objects.emplace_back(old_to_new[info.obj_id], obj);
  }

  std::map<ae::ObjId, ae::Ptr<ae::Obj>> new_id_to_obj;
  for (auto& [new_id, obj] : loaded_objects) {
    obj->obj_id = new_id;
    obj->domain = &receiver_domain;
    new_id_to_obj[new_id] = obj;
  }

  for (auto& [new_id, obj] : loaded_objects) {
    receiver_domain.AddObject(new_id, obj);
  }

  bool remap_ok = true;
  detail::OperationStorage remap_storage{
      detail::OperationMode::kRemap,
      /*validation_storage=*/nullptr,
      /*most_derived_classes=*/nullptr,
      &receiver_domain,
      &receiver_storage,
      &old_to_new,
      &new_id_to_obj,
      &remap_ok};

  ae::Domain remap_domain{remap_storage};
  ae::DomainGraph remap_graph{&remap_domain,
                              ae::GraphSerializationScope::NetworkShared};

  for (auto& [new_id, obj] : loaded_objects) {
    remap_graph.SaveRoot(obj, new_id);
  }

  if (!remap_ok) {
    return {};
  }

  auto receiver_root = receiver_domain.Find(old_to_new[root_event_id]);
  return ae::Ptr<Event>{receiver_root};
}

namespace {

bool CopyStoredObjectAs(
    ae::RamDomainStorage const& source,
    ae::ObjId source_id,
    ae::ObjId destination_id,
    ae::IDomainStorage& destination) {
  auto const it = source.state.find(source_id);
  if (it == source.state.end() || !it->second.has_value()) {
    return false;
  }
  for (auto const& [class_id, versions] : *it->second) {
    for (auto const& [version, data] : versions) {
      auto writer = destination.Store(
          ae::DomainQuery{destination_id, class_id, version});
      if (writer == nullptr) {
        return false;
      }
      if (!data.empty()) {
        auto const result = writer->Write(
            ae::seri::DataWriteTag{data.data(), data.size()});
        if (!result) {
          return false;
        }
      }
    }
  }
  return true;
}

}  // namespace

bool ValidateStandaloneEventGraph(
    ae::RamDomainStorage const& parsed,
    ae::ObjId root_id,
    std::uint32_t expected_event_class_id) {
  if (!root_id.is_valid()) {
    return false;
  }

  std::vector<StoredClassChainInfo> chains;
  if (!ValidateStoredClassChains(parsed, &chains)) {
    return false;
  }

  std::size_t active_count = 0;
  ae::ObjId only_id;
  for (auto const& [obj_id, classes] : parsed.state) {
    if (classes.has_value()) {
      ++active_count;
      only_id = obj_id;
    }
  }
  if (active_count != 1 || only_id != root_id) {
    return false;
  }

  auto const chain_it =
      std::find_if(chains.begin(), chains.end(),
                   [&](StoredClassChainInfo const& c) {
                     return c.obj_id == root_id;
                   });
  if (chain_it == chains.end()) {
    return false;
  }

  if (chain_it->most_derived_class_id != expected_event_class_id) {
    return false;
  }

  auto& reg = ae::Registry::GetRegistry();
  if (reg.GenerationDistance(Event::kClassId, expected_event_class_id) < 0) {
    return false;
  }

  ae::RamDomainStorage scratch = parsed;
  ae::Domain scratch_domain{scratch};
  ae::DomainGraph graph{&scratch_domain};
  auto loaded = graph.LoadRoot(root_id);
  if (!loaded) {
    return false;
  }
  if (loaded->GetClassId() != expected_event_class_id) {
    return false;
  }

  return true;
}

ae::Ptr<Event> ImportStandaloneEventGraph(
    ae::RamDomainStorage const& parsed,
    ae::ObjId root_id,
    std::uint32_t expected_event_class_id,
    ae::Domain& receiver_domain,
    ae::IDomainStorage& receiver_storage) {
  if (!ValidateStandaloneEventGraph(parsed, root_id, expected_event_class_id)) {
    return {};
  }

  std::set<ae::ObjId> reserved_ids;
  auto const destination_id =
      AllocateUniqueReceiverObjId(receiver_domain, receiver_storage, reserved_ids);
  if (!destination_id.is_valid()) {
    return {};
  }

  if (!CopyStoredObjectAs(parsed, root_id, destination_id, receiver_storage)) {
    return {};
  }

  ae::DomainGraph receiver_graph{&receiver_domain};
  auto loaded = receiver_graph.LoadRoot(destination_id);
  if (!loaded) {
    return {};
  }
  if (loaded->GetClassId() != expected_event_class_id) {
    return {};
  }

  return ae::Ptr<Event>{loaded};
}

}  // namespace apptraverse
