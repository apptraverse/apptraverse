#include "apptraverse/object_serialization.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <unordered_set>

#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/registry.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/graph_walk.h"

namespace apptraverse {
namespace {

void SaveObjectGraphToScratch(ae::Obj const& object,
                              ae::RamDomainStorage& scratch) {
  ae::Domain scratch_domain{ae::Now(), scratch};
  ae::DomainGraph graph{&scratch_domain};
  auto ptr = object.domain->Find(object.obj_id);
  assert(ptr);
  auto* factory = ae::Registry::GetRegistry().FindFactory(object.GetClassId());
  assert(factory != nullptr);
  assert(factory->save != nullptr);
  factory->save(&graph, ptr, object.obj_id);
}

void InjectObjectBytes(ae::IDomainStorage& storage, ae::DomainQuery const& query,
                       std::uint8_t const* data, std::size_t size) {
  ae::ObjectData bytes(data, data + size);
  if (auto* ram = dynamic_cast<ae::RamDomainStorage*>(&storage)) {
    ram->SaveData(query, std::move(bytes));
    return;
  }
  if (auto* dir = dynamic_cast<DirectoryDomainStorage*>(&storage)) {
    auto class_dir = dir->root() / std::to_string(query.id.id()) /
                     std::to_string(query.class_id);
    std::filesystem::create_directories(class_dir);
    auto const path = class_dir / std::to_string(query.version);
    std::ofstream out{path, std::ios::out | std::ios::binary | std::ios::trunc};
    out.write(reinterpret_cast<char const*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    assert(out.good());
    return;
  }
  assert(false && "unsupported IDomainStorage for object buffer injection");
}

void AppendSavedObjectLayers(ae::RamDomainStorage const& scratch, ae::ObjId id,
                             ByteSink& out) {
  auto obj_it = scratch.state.find(id);
  assert(obj_it != scratch.state.end());
  assert(obj_it->second.has_value());
  for (auto const& [class_id, versions] : *obj_it->second) {
    for (auto const& [version, data] : versions) {
      out.write(&class_id, sizeof(class_id));
      out.write(&version, sizeof(version));
      auto const size = static_cast<std::uint32_t>(data.size());
      out.write(&size, sizeof(size));
      out.write(data.data(), data.size());
    }
  }
}

void InjectSavedObjectLayers(ByteSource& in, ae::ObjId id,
                             ae::IDomainStorage& storage,
                             std::size_t payload_size) {
  std::size_t const end = in.pos + payload_size;
  while (in.pos < end) {
    std::uint32_t class_id = 0;
    std::uint8_t version = 0;
    std::uint32_t size = 0;
    in.read(&class_id, sizeof(class_id));
    in.read(&version, sizeof(version));
    in.read(&size, sizeof(size));
    assert(in.ok && in.pos + size <= end);
    InjectObjectBytes(storage, {id, class_id, version}, in.data + in.pos, size);
    in.pos += size;
  }
  assert(in.pos == end);
}

void RemoveDistilledBaseObjects(std::vector<ae::Obj*>& objects) {
  std::unordered_set<std::uint32_t> base_ids;
  for (ae::Obj* obj : objects) {
    if (auto* node = dynamic_cast<Node*>(obj)) {
      if (node->base.is_valid()) {
        base_ids.insert(node->base.id().id());
      }
    }
  }
  if (base_ids.empty()) {
    return;
  }
  objects.erase(
      std::remove_if(objects.begin(), objects.end(),
                     [&](ae::Obj* obj) {
                       return base_ids.count(obj->obj_id.id()) > 0;
                     }),
      objects.end());
}

}  // namespace

void SerializeObjectToBuffer(ae::Obj const& object, ByteSink& out) {
  ae::RamDomainStorage scratch;
  SaveObjectGraphToScratch(object, scratch);
  AppendSavedObjectLayers(scratch, object.obj_id, out);
}

void DeserializeObjectFromBuffer(ae::Obj& object, ByteSource& in,
                                 ae::Domain& domain,
                                 ae::IDomainStorage& domain_storage) {
  std::size_t const payload_size = in.size - in.pos;
  InjectSavedObjectLayers(in, object.obj_id, domain_storage, payload_size);

  ae::DomainGraph graph{&domain};
  auto ptr = domain.Find(object.obj_id);
  assert(ptr);
  auto* factory = ae::Registry::GetRegistry().FindFactory(object.GetClassId());
  assert(factory != nullptr);
  assert(factory->load != nullptr);
  factory->load(&graph, ptr, object.obj_id);
}

void CollectReachableObjects(ae::Obj& root, std::vector<ae::Obj*>& out) {
  ae::RamDomainStorage scratch;
  SaveObjectGraphToScratch(root, scratch);

  out.clear();
  out.reserve(scratch.state.size());
  for (auto const& [obj_id, class_map_opt] : scratch.state) {
    if (!class_map_opt) {
      continue;
    }
    if (auto obj = root.domain->Find(obj_id)) {
      out.push_back(&*obj);
    }
  }
  RemoveDistilledBaseObjects(out);
}

void CollectReachableNodes(ae::Obj& root, std::vector<Node*>& out) {
  std::vector<ae::Obj*> objects;
  CollectReachableObjects(root, objects);
  for (ae::Obj* obj : objects) {
    if (auto* node = dynamic_cast<Node*>(obj)) {
      out.push_back(node);
    }
  }
}

void EagerLoadReachable(ae::Obj& root) {
  std::vector<ae::Obj*> objects;
  CollectReachableObjects(root, objects);
  ae::DomainGraph graph{root.domain};
  for (ae::Obj* obj : objects) {
    auto ptr = root.domain->Find(obj->obj_id);
    assert(ptr);
    auto* factory = ae::Registry::GetRegistry().FindFactory(obj->GetClassId());
    assert(factory != nullptr);
    assert(factory->load != nullptr);
    factory->load(&graph, ptr, obj->obj_id);
  }
}

void FinalizeUiNodeState(ae::Obj& object, std::uint64_t generation) {
  if (auto* node = dynamic_cast<Node*>(&object)) {
    node->AdoptPublishedGeneration(generation);
    node->base = {};
    node->journal.clear();
  }
}

}  // namespace apptraverse
