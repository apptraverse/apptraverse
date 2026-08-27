#include "apptraverse/object_serialization.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <unordered_set>

#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/registry.h"

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
  auto writer = storage.Store(query);
  assert(writer);
  auto const result = writer->Write(ae::seri::DataWriteTag{data, size});
  assert(result);
  (void)result;
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

void LoadExistingObject(ae::Obj& object, ae::Domain& domain) {
  ae::DomainGraph graph{&domain};
  auto ptr = domain.Find(object.obj_id);
  assert(ptr);
  auto* factory = ae::Registry::GetRegistry().FindFactory(object.GetClassId());
  assert(factory != nullptr);
  assert(factory->load != nullptr);
  factory->load(&graph, ptr, object.obj_id);
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
  LoadExistingObject(object, domain);
}

void SerializeObjectGraphToBuffer(ae::Obj const& root, ByteSink& out) {
  ae::RamDomainStorage scratch;
  SaveObjectGraphToScratch(root, scratch);

  auto const count_at = out.bytes.size();
  std::uint32_t layer_count = 0;
  out.write(&layer_count, sizeof(layer_count));

  for (auto const& [obj_id, class_map_opt] : scratch.state) {
    if (!class_map_opt) {
      continue;
    }
    for (auto const& [class_id, versions] : *class_map_opt) {
      for (auto const& [version, data] : versions) {
        auto const oid = obj_id.id();
        out.write(&oid, sizeof(oid));
        out.write(&class_id, sizeof(class_id));
        out.write(&version, sizeof(version));
        auto const size = static_cast<std::uint32_t>(data.size());
        out.write(&size, sizeof(size));
        out.write(data.data(), data.size());
        ++layer_count;
      }
    }
  }
  std::memcpy(out.bytes.data() + count_at, &layer_count, sizeof(layer_count));
}

void DeserializeObjectGraphFromBuffer(ae::Obj& existing_root, ByteSource& in,
                                      ae::Domain& domain,
                                      ae::IDomainStorage& domain_storage) {
  std::uint32_t layer_count = 0;
  in.read(&layer_count, sizeof(layer_count));
  assert(in.ok);

  for (std::uint32_t i = 0; i < layer_count; ++i) {
    std::uint32_t obj_id = 0;
    std::uint32_t class_id = 0;
    std::uint8_t version = 0;
    std::uint32_t size = 0;
    in.read(&obj_id, sizeof(obj_id));
    in.read(&class_id, sizeof(class_id));
    in.read(&version, sizeof(version));
    in.read(&size, sizeof(size));
    assert(in.ok && in.pos + size <= in.size);
    InjectObjectBytes(domain_storage, {ae::ObjId{obj_id}, class_id, version},
                      in.data + in.pos, size);
    in.pos += size;
  }

  LoadExistingObject(existing_root, domain);
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

void FinalizeUiNodeState(ae::Obj& object, std::uint64_t generation) {
  if (auto* node = dynamic_cast<Node*>(&object)) {
    node->AdoptPublishedGeneration(generation);
    node->base = {};
    node->journal.clear();
  }
}

}  // namespace apptraverse
