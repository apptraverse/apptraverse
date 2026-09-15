#include "apptraverse/shared_network_graph.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <set>
#include <utility>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/registry.h"
#include "apptraverse/event.h"
#include "apptraverse/node.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/remap_pointers.h"

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

void AppendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

bool ReadU32(std::vector<std::uint8_t> const& in, std::size_t& pos,
             std::uint32_t& value) {
  if (pos + 4 > in.size()) {
    return false;
  }
  value = (static_cast<std::uint32_t>(in[pos]) << 24U) |
          (static_cast<std::uint32_t>(in[pos + 1]) << 16U) |
          (static_cast<std::uint32_t>(in[pos + 2]) << 8U) |
          static_cast<std::uint32_t>(in[pos + 3]);
  pos += 4;
  return true;
}

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

std::vector<std::uint8_t> SerializeRamDomainStorage(
    ae::RamDomainStorage const& storage) {
  std::vector<std::uint8_t> out;
  std::uint32_t object_count = 0;
  for (auto const& [obj_id, classes] : storage.state) {
    if (classes.has_value()) {
      ++object_count;
    }
  }
  AppendU32(out, object_count);

  for (auto const& [obj_id, classes] : storage.state) {
    if (!classes.has_value()) {
      continue;
    }
    AppendU32(out, obj_id.id());
    AppendU32(out, static_cast<std::uint32_t>(classes->size()));
    for (auto const& [class_id, versions] : *classes) {
      AppendU32(out, class_id);
      AppendU32(out, static_cast<std::uint32_t>(versions.size()));
      for (auto const& [version, data] : versions) {
        out.push_back(version);
        AppendU32(out, static_cast<std::uint32_t>(data.size()));
        out.insert(out.end(), data.begin(), data.end());
      }
    }
  }
  return out;
}

std::vector<std::uint8_t> SerializeNetworkSharedObjectGraph(
    ae::Obj const& root) {
  ae::RamDomainStorage scratch;
  BuildNetworkSharedScratch(root, scratch);
  return SerializeRamDomainStorage(scratch);
}

FrozenNodeState FreezeNetworkSharedNodeState(SharedNode const& root) {
  ae::RamDomainStorage scratch;
  BuildNetworkSharedScratch(root, scratch);

  // Extract covered SharedEventIds from the scratch snapshot of root.
  // Reconstruct root in scratch domain to inspect the exact frozen journal.
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
      .payload = SerializeRamDomainStorage(scratch),
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

bool ValidateStandaloneEventStorage(
    ae::RamDomainStorage const& parsed,
    std::uint32_t expected_event_class_id) {
  auto& registry = ae::Registry::GetRegistry();
  if (registry.GenerationDistance(Event::kClassId, expected_event_class_id) < 0) {
    return false;
  }

  std::vector<StoredClassChainInfo> chains;
  if (!ValidateStoredClassChains(parsed, &chains)) {
    return false;
  }

  if (chains.size() != 1) {
    return false;
  }
  auto const& info = chains.front();
  if (info.obj_id != kStandaloneEventScratchId) {
    return false;
  }
  if (info.most_derived_class_id != expected_event_class_id) {
    return false;
  }

  // Ensure the expected_event_class_id layer is actually present in storage.
  auto const it = parsed.state.find(kStandaloneEventScratchId);
  if (it == parsed.state.end() || !it->second.has_value()) {
    return false;
  }
  if (it->second->find(expected_event_class_id) == it->second->end()) {
    return false;
  }

  return true;
}

bool ParseObjectGraphPayload(std::vector<std::uint8_t> const& payload,
                             ae::RamDomainStorage& parsed) {
  std::size_t pos = 0;
  std::uint32_t object_count = 0;
  if (!ReadU32(payload, pos, object_count)) {
    return false;
  }
  std::set<std::uint32_t> seen_objects;
  for (std::uint32_t object = 0; object < object_count; ++object) {
    std::uint32_t obj_id = 0;
    std::uint32_t class_count = 0;
    if (!ReadU32(payload, pos, obj_id) || !ReadU32(payload, pos, class_count)) {
      return false;
    }
    if (!ae::ObjId{obj_id}.is_valid()) {
      return false;
    }
    if (!seen_objects.insert(obj_id).second) {
      // Duplicate object entry
      return false;
    }
    std::set<std::uint32_t> seen_classes;
    for (std::uint32_t klass = 0; klass < class_count; ++klass) {
      std::uint32_t class_id = 0;
      std::uint32_t version_count = 0;
      if (!ReadU32(payload, pos, class_id) ||
          !ReadU32(payload, pos, version_count)) {
        return false;
      }
      if (!seen_classes.insert(class_id).second) {
        // Duplicate class entry
        return false;
      }
      std::set<std::uint8_t> seen_versions;
      for (std::uint32_t version_index = 0; version_index < version_count;
           ++version_index) {
        if (pos >= payload.size()) {
          return false;
        }
        auto const version = payload[pos++];
        if (!seen_versions.insert(version).second) {
          // Duplicate version entry
          return false;
        }
        std::uint32_t size = 0;
        if (!ReadU32(payload, pos, size) || pos + size > payload.size()) {
          return false;
        }
        parsed.SaveData(
            ae::DomainQuery{ae::ObjId{obj_id}, class_id, version},
            ae::ObjectData{payload.begin() + static_cast<std::ptrdiff_t>(pos),
                           payload.begin() +
                               static_cast<std::ptrdiff_t>(pos + size)});
        pos += size;
      }
    }
  }
  return pos == payload.size();
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
  if (!ParseObjectGraphPayload(payload, parsed)) {
    return false;
  }
  CommitObjectGraph(parsed, target_storage);
  return true;
}

bool FreezeStandaloneEventPayload(ae::Obj const& event,
                                  std::vector<std::uint8_t>& out) {
  ae::RamDomainStorage scratch;
  BuildNetworkSharedScratch(event, scratch);

  ae::ObjId sole{};
  std::uint32_t object_count = 0;
  for (auto const& [obj_id, classes] : scratch.state) {
    if (!classes.has_value()) {
      continue;
    }
    ++object_count;
    sole = obj_id;
  }
  // V1 refuses any Event whose graph reaches another object.
  if (object_count != 1) {
    return false;
  }

  auto const it = scratch.state.find(sole);
  if (it == scratch.state.end() || !it->second.has_value()) {
    return false;
  }
  auto const& classes = *it->second;

  out.clear();
  AppendU32(out, static_cast<std::uint32_t>(classes.size()));
  for (auto const& [class_id, versions] : classes) {
    AppendU32(out, class_id);
    AppendU32(out, static_cast<std::uint32_t>(versions.size()));
    for (auto const& [version, data] : versions) {
      out.push_back(version);
      AppendU32(out, static_cast<std::uint32_t>(data.size()));
      out.insert(out.end(), data.begin(), data.end());
    }
  }
  return true;
}

bool ParseStandaloneEventPayload(std::vector<std::uint8_t> const& payload,
                                 ae::RamDomainStorage& parsed) {
  std::size_t pos = 0;
  std::uint32_t class_count = 0;
  if (!ReadU32(payload, pos, class_count) || class_count == 0) {
    return false;
  }
  std::set<std::uint32_t> seen_classes;
  for (std::uint32_t klass = 0; klass < class_count; ++klass) {
    std::uint32_t class_id = 0;
    std::uint32_t version_count = 0;
    if (!ReadU32(payload, pos, class_id) ||
        !ReadU32(payload, pos, version_count) || version_count == 0) {
      return false;
    }
    if (!seen_classes.insert(class_id).second) {
      // Duplicate class entry
      return false;
    }
    std::set<std::uint8_t> seen_versions;
    for (std::uint32_t version_index = 0; version_index < version_count;
         ++version_index) {
      if (pos >= payload.size()) {
        return false;
      }
      auto const version = payload[pos++];
      if (!seen_versions.insert(version).second) {
        // Duplicate version entry
        return false;
      }
      std::uint32_t size = 0;
      if (!ReadU32(payload, pos, size) || pos + size > payload.size()) {
        return false;
      }
      parsed.SaveData(
          ae::DomainQuery{kStandaloneEventScratchId, class_id, version},
          ae::ObjectData{payload.begin() + static_cast<std::ptrdiff_t>(pos),
                         payload.begin() +
                             static_cast<std::ptrdiff_t>(pos + size)});
      pos += size;
    }
  }
  return pos == payload.size();
}

void CommitStandaloneEventObject(ae::RamDomainStorage const& parsed,
                                 ae::ObjId local_id,
                                 ae::IDomainStorage& target_storage) {
  assert(local_id.is_valid());
  auto const it = parsed.state.find(kStandaloneEventScratchId);
  assert(it != parsed.state.end() && it->second.has_value());
  for (auto const& [class_id, versions] : *it->second) {
    for (auto const& [version, data] : versions) {
      auto writer =
          target_storage.Store(ae::DomainQuery{local_id, class_id, version});
      assert(writer != nullptr);
      if (!data.empty()) {
        auto const result =
            writer->Write(ae::seri::DataWriteTag{data.data(), data.size()});
        assert(result);
        (void)result;
      }
    }
  }
}

namespace {

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

void RemapObjectPointers(
    ae::Obj& obj, ae::Domain* target_domain,
    std::map<ae::ObjId, ae::ObjId> const& mapping) {
  auto const class_id = obj.GetClassId();
  auto& reg = ae::Registry::GetRegistry();
  if (reg.GenerationDistance(Event::kClassId, class_id) >= 0) {
    static_cast<Event&>(obj).RemapPointers(target_domain, mapping);
  } else if (reg.GenerationDistance(Node::kClassId, class_id) >= 0) {
    static_cast<Node&>(obj).RemapPointers(target_domain, mapping);
  }
}

bool ValidateObjectPointers(ae::Obj const& obj,
                            ae::RamDomainStorage const& storage) {
  auto const class_id = obj.GetClassId();
  auto& reg = ae::Registry::GetRegistry();
  if (reg.GenerationDistance(Event::kClassId, class_id) >= 0) {
    return static_cast<Event const&>(obj).ValidatePointers(storage);
  } else if (reg.GenerationDistance(Node::kClassId, class_id) >= 0) {
    return static_cast<Node const&>(obj).ValidatePointers(storage);
  }
  return true;
}

}  // namespace

bool FreezeClosedEventGraphPayload(
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
  AppendU32(out_payload, event.obj_id.id());
  auto const storage_bytes = SerializeRamDomainStorage(scratch);
  out_payload.insert(out_payload.end(), storage_bytes.begin(),
                     storage_bytes.end());
  return true;
}

bool ParseClosedEventGraphPayload(
    std::vector<std::uint8_t> const& payload,
    ae::RamDomainStorage& parsed,
    ae::ObjId& out_root_id) {
  std::size_t pos = 0;
  std::uint32_t root_id_raw = 0;
  if (!ReadU32(payload, pos, root_id_raw)) {
    return false;
  }
  out_root_id = ae::ObjId{root_id_raw};
  if (!out_root_id.is_valid()) {
    return false;
  }

  std::vector<std::uint8_t> const storage_payload{
      payload.begin() + static_cast<std::ptrdiff_t>(pos), payload.end()};
  if (!ParseObjectGraphPayload(storage_payload, parsed)) {
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

  for (auto const& info : chains) {
    auto obj = scratch_graph.LoadRoot(info.obj_id);
    if (!obj) {
      return false;
    }
    if (!ValidateObjectPointers(*obj, scratch_copy)) {
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

  for (auto& [new_id, obj] : loaded_objects) {
    obj->obj_id = new_id;
    obj->domain = &receiver_domain;
    RemapObjectPointers(*obj, &receiver_domain, old_to_new);
  }

  ae::DomainGraph save_graph{&receiver_domain,
                             ae::GraphSerializationScope::NetworkShared};
  for (auto& [new_id, obj] : loaded_objects) {
    save_graph.SaveRoot(obj, new_id);
    receiver_domain.AddObject(new_id, obj);
  }

  auto receiver_root = receiver_domain.Find(old_to_new[root_event_id]);
  return ae::Ptr<Event>{receiver_root};
}

}  // namespace apptraverse
