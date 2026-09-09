#include "apptraverse/object_serialization.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <utility>
#include <unordered_set>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/registry.h"

#include "apptraverse/graph_walk.h"
#include "apptraverse/presenter.h"

namespace apptraverse {
namespace {

void SaveObjectGraphToScratch(ae::Obj const& object,
                              ae::RamDomainStorage& scratch) {
  ae::Domain scratch_domain{scratch};
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

  std::unordered_set<std::uint32_t> distilled_base_ids;
  for (auto const& [obj_id, class_map_opt] : scratch.state) {
    if (!class_map_opt) {
      continue;
    }
    auto obj = root.domain->Find(obj_id);
    if (!obj) {
      continue;
    }
    if (auto* node = dynamic_cast<Node*>(obj.get())) {
      if (node->base.is_valid()) {
        distilled_base_ids.insert(node->base.id().id());
      }
    }
  }

  auto const count_at = out.bytes.size();
  std::uint32_t layer_count = 0;
  out.write(&layer_count, sizeof(layer_count));

  for (auto const& [obj_id, class_map_opt] : scratch.state) {
    if (!class_map_opt) {
      continue;
    }
    if (distilled_base_ids.count(obj_id.id()) != 0) {
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

  auto const gen_count_at = out.bytes.size();
  std::uint32_t node_generation_count = 0;
  out.write(&node_generation_count, sizeof(node_generation_count));
  for (auto const& [obj_id, class_map_opt] : scratch.state) {
    if (!class_map_opt) {
      continue;
    }
    if (distilled_base_ids.count(obj_id.id()) != 0) {
      continue;
    }
    auto obj = root.domain->Find(obj_id);
    if (!obj) {
      continue;
    }
    auto* node = dynamic_cast<Node*>(obj.get());
    if (node == nullptr) {
      continue;
    }
    auto const oid = obj_id.id();
    auto const generation = node->Generation();
    out.write(&oid, sizeof(oid));
    out.write(&generation, sizeof(generation));
    ++node_generation_count;
  }
  std::memcpy(out.bytes.data() + gen_count_at, &node_generation_count,
              sizeof(node_generation_count));
}

void DeserializeObjectGraphFromBuffer(ae::Obj& existing_root, ByteSource& in,
                                      ae::Domain& domain,
                                      ae::IDomainStorage& domain_storage) {
  std::uint32_t layer_count = 0;
  in.read(&layer_count, sizeof(layer_count));
  assert(in.ok);

  // Load each published object with its own DomainGraph (same pattern as
  // CopyModelGraphToUiDomain). Nested LoadRoot alone can return an empty UI
  // shell that was ConstructObj'd earlier in the shared graph walk without
  // reloading bytes from storage — which left ImmutableString bodies empty.
  std::vector<ae::ObjId> object_ids;
  object_ids.reserve(layer_count);
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
    ae::ObjId const id{obj_id};
    if (std::find(object_ids.begin(), object_ids.end(), id) ==
        object_ids.end()) {
      object_ids.push_back(id);
    }
  }

  // First load the published root (may ConstructObj empty shells for newly
  // referenced objects when nested LoadRoot short-circuits). Then refresh every
  // injected object with a fresh DomainGraph so storage bytes (e.g. message
  // bodies) actually populate those shells — matching CopyModelGraphToUiDomain.
  LoadExistingObject(existing_root, domain);
  for (ae::ObjId const id : object_ids) {
    if (auto object = domain.Find(id); object) {
      LoadExistingObject(*object, domain);
    }
  }

  std::uint32_t node_generation_count = 0;
  in.read(&node_generation_count, sizeof(node_generation_count));
  assert(in.ok);
  for (std::uint32_t i = 0; i < node_generation_count; ++i) {
    std::uint32_t obj_id = 0;
    std::uint64_t generation = 0;
    in.read(&obj_id, sizeof(obj_id));
    in.read(&generation, sizeof(generation));
    assert(in.ok);
    auto object = domain.Find(ae::ObjId{obj_id});
    if (!object) {
      continue;
    }
    FinalizeUiNodeState(*object, generation);
  }
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

void CollectLiveReachableObjects(ae::Obj& root, std::vector<ae::Obj*>& out) {
  // Clear Node bookkeeping temporarily so Save-based reachability follows
  // live fields only (e.g. ItemList::items), not journal/base history.
  std::vector<ae::Obj*> with_history;
  CollectReachableObjects(root, with_history);
  struct SavedBookkeeping {
    Node* node;
    Node::ptr base;
    std::vector<EventRecord> journal;
  };
  std::vector<SavedBookkeeping> saved;
  saved.reserve(with_history.size());
  for (ae::Obj* obj : with_history) {
    auto* node = dynamic_cast<Node*>(obj);
    if (node == nullptr) {
      continue;
    }
    saved.push_back(SavedBookkeeping{node, node->base, std::move(node->journal)});
    node->base = {};
    node->journal.clear();
  }
  CollectReachableObjects(root, out);
  for (SavedBookkeeping& entry : saved) {
    entry.node->base = std::move(entry.base);
    entry.node->journal = std::move(entry.journal);
  }
}

void FinalizeUiNodeState(ae::Obj& object, std::uint64_t generation) {
  if (auto* node = dynamic_cast<Node*>(&object)) {
    node->AdoptPublishedGeneration(generation);
    node->base = {};
    node->journal.clear();
  }
}

void SerializeIncrementalNodePublication(Node const& node, ByteSink& out) {
  auto const object_id = node.obj_id.id();
  auto const generation = node.Generation();
  ByteSink payload;
  SerializeObjectToBuffer(node, payload);
  auto const payload_size = static_cast<std::uint32_t>(payload.bytes.size());
  out.write(&object_id, sizeof(object_id));
  out.write(&generation, sizeof(generation));
  out.write(&payload_size, sizeof(payload_size));
  out.write(payload.bytes.data(), payload.bytes.size());
}

ae::Obj& ApplyIncrementalPublication(ByteSource& in, ae::Domain& domain,
                                     ae::IDomainStorage& storage) {
  std::uint32_t object_id = 0;
  std::uint64_t generation = 0;
  std::uint32_t payload_size = 0;
  in.read(&object_id, sizeof(object_id));
  in.read(&generation, sizeof(generation));
  in.read(&payload_size, sizeof(payload_size));
  assert(in.ok);
  assert(in.pos + payload_size <= in.size);
  ByteSource payload;
  payload.data = in.data + in.pos;
  payload.size = payload_size;
  in.pos += payload_size;
  auto object = domain.Find(ae::ObjId{object_id});
  assert(object && "incremental publication object must already exist");
  DeserializeObjectFromBuffer(*object, payload, domain, storage);
  FinalizeUiNodeState(*object, generation);
  return *object;
}

void SerializeStructuralNodePublication(Node const& node, ByteSink& out) {
  auto const object_id = node.obj_id.id();
  auto const generation = node.Generation();
  ByteSink payload;
  SerializeObjectGraphToBuffer(node, payload);
  auto const payload_size = static_cast<std::uint32_t>(payload.bytes.size());
  out.write(&object_id, sizeof(object_id));
  out.write(&generation, sizeof(generation));
  out.write(&payload_size, sizeof(payload_size));
  out.write(payload.bytes.data(), payload.bytes.size());
}

ae::Obj& ApplyStructuralPublication(ByteSource& in, ae::Domain& domain,
                                    ae::IDomainStorage& storage) {
  std::uint32_t object_id = 0;
  std::uint64_t generation = 0;
  std::uint32_t payload_size = 0;
  in.read(&object_id, sizeof(object_id));
  in.read(&generation, sizeof(generation));
  in.read(&payload_size, sizeof(payload_size));
  assert(in.ok);
  assert(in.pos + payload_size <= in.size);
  ByteSource payload;
  payload.data = in.data + in.pos;
  payload.size = payload_size;
  in.pos += payload_size;
  auto object = domain.Find(ae::ObjId{object_id});
  assert(object && "structural publication object must already exist");
  DeserializeObjectGraphFromBuffer(*object, payload, domain, storage);
  FinalizeUiNodeState(*object, generation);
  return *object;
}

void SerializeInitialPublication(ae::Obj const& root, ByteSink& out) {
  std::vector<Node*> nodes;
  CollectReachableNodes(const_cast<ae::Obj&>(root), nodes);
  for (Node* node : nodes) {
    node->EnsureCurrentGeneration();
  }
  auto const root_id = root.obj_id.id();
  out.write(&root_id, sizeof(root_id));
  SerializeObjectGraphToBuffer(root, out);
}

ae::Ptr<ae::Obj> LoadInitialPublication(ByteSource& in, ae::Domain& ui_domain,
                                      ae::IDomainStorage& ui_storage) {
  std::uint32_t root_id = 0;
  in.read(&root_id, sizeof(root_id));
  assert(in.ok);

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
    InjectObjectBytes(ui_storage, {ae::ObjId{obj_id}, class_id, version},
                      in.data + in.pos, size);
    in.pos += size;
  }

  // Create objects through DomainGraph::LoadRoot so aether-objects can pick
  // the most-derived registered factory for the stored class layers. Do not
  // select a factory in App Traverse.
  ae::DomainGraph graph{&ui_domain};
  auto ui_root = graph.LoadRoot(ae::ObjId{root_id});
  assert(ui_root);

  std::uint32_t node_generation_count = 0;
  in.read(&node_generation_count, sizeof(node_generation_count));
  assert(in.ok);
  for (std::uint32_t i = 0; i < node_generation_count; ++i) {
    std::uint32_t obj_id = 0;
    std::uint64_t generation = 0;
    in.read(&obj_id, sizeof(obj_id));
    in.read(&generation, sizeof(generation));
    assert(in.ok);
    auto object = ui_domain.Find(ae::ObjId{obj_id});
    if (!object) {
      continue;
    }
    FinalizeUiNodeState(*object, generation);
  }

  ui_root = ui_domain.Find(ae::ObjId{root_id});
  assert(ui_root && "UI mirror graph must stay reachable via ObjPtr refs");
  return ui_root;
}

void InitializeNewPresenters(ae::Obj& gui_root, void* host) {
  // Multipass: child presenters may wait until a parent HWND exists.
  for (;;) {
    bool progress = false;
    std::vector<ae::Obj*> objects;
    CollectLiveReachableObjects(gui_root, objects);
    for (ae::Obj* obj : objects) {
      auto* presenter = dynamic_cast<Presenter*>(obj);
      if (presenter == nullptr || presenter->presentation_loaded) {
        continue;
      }
      if (!presenter->ReadyForPresentation()) {
        continue;
      }
      presenter->presentation_host = host;
      presenter->OnLoad();
      presenter->presentation_loaded = true;
      progress = true;
    }
    if (!progress) {
      break;
    }
  }
}

void InitializePresenters(ae::Obj& gui_root, void* host) {
  InitializeNewPresenters(gui_root, host);
}

void UnloadPresenters(ae::Obj& gui_root) {
  std::vector<ae::Obj*> objects;
  CollectLiveReachableObjects(gui_root, objects);
  // Reverse of typical root-first Save/ObjId collect order so child HWNDs are
  // destroyed before parents (DestroyWindow on a parent destroys children).
  for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
    auto* presenter = dynamic_cast<Presenter*>(*it);
    if (presenter == nullptr || !presenter->presentation_loaded) {
      continue;
    }
    presenter->OnUnload();
    presenter->presentation_loaded = false;
  }
}

void UpdatePresentersAfterStructuralPublication(
    ae::Obj& gui_root, std::vector<Presenter*> const& previously_active,
    void* host) {
  std::vector<ae::Obj*> live_objects;
  CollectLiveReachableObjects(gui_root, live_objects);
  std::unordered_set<Presenter*> live_presenters;
  for (ae::Obj* obj : live_objects) {
    if (auto* presenter = dynamic_cast<Presenter*>(obj)) {
      live_presenters.insert(presenter);
    }
  }
  for (Presenter* presenter : previously_active) {
    if (presenter == nullptr || !presenter->presentation_loaded) {
      continue;
    }
    if (live_presenters.count(presenter) != 0) {
      continue;
    }
    presenter->OnUnload();
    presenter->presentation_loaded = false;
  }
  InitializeNewPresenters(gui_root, host);
}

}  // namespace apptraverse
