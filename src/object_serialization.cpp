#include "apptraverse/object_serialization.h"
#include "apptraverse/publication_serialization_scope.h"
#include "apptraverse/event.h"
#include "apptraverse/surfaces/surfaces_model.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <utility>
#include <unordered_set>
#include <vector>

#include "aether-miscpp/reflect/reflect.h"
#include "aether-miscpp/serialization/binary_archive.h"

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/registry.h"

#include "apptraverse/graph_walk.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/presenter.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse {
namespace {

LiveGraphEdgeAppender g_live_graph_edge_appender = nullptr;

enum StructuralPublicationSidecar : std::uint8_t {
  kStructuralPublicationSidecarNone = 0,
  kStructuralPublicationSidecarUiDesktopFrameRequest = 1,
};

struct StructuralPublicationEnvelope {
  std::uint32_t object_id{};
  std::uint64_t generation{};
  std::vector<std::uint8_t> graph_bytes;
  std::uint8_t sidecar{};
  AE_REFLECT_MEMBERS(object_id, generation, graph_bytes, sidecar)
};

struct InitialPublicationEnvelope {
  std::uint32_t root_id{};
  std::vector<std::uint8_t> graph_bytes;
  AE_REFLECT_MEMBERS(root_id, graph_bytes)
};

struct IncrementalPublicationEnvelope {
  std::uint32_t object_id;
  std::uint64_t generation;
  std::vector<std::uint8_t> object_bytes;
  AE_REFLECT_MEMBERS(object_id, generation, object_bytes)
};

struct GraphPublicationNodeGeneration {
  std::uint32_t object_id{};
  std::uint64_t generation{};
  AE_REFLECT_MEMBERS(object_id, generation)
};

struct GraphPublicationPayload {
  ae::RamDomainStorage::ObjClassData layers;
  std::vector<GraphPublicationNodeGeneration> generations;
  AE_REFLECT_MEMBERS(layers, generations)
};

template <typename T>
T* AsObjOf(ae::Obj* obj) {
  if (obj == nullptr) {
    return nullptr;
  }
  if (ae::Registry::GetRegistry().GenerationDistance(T::kClassId,
                                                     obj->GetClassId()) < 0) {
    return nullptr;
  }
  return static_cast<T*>(obj);
}

template <typename T>
T const* AsObjOf(ae::Obj const* obj) {
  if (obj == nullptr) {
    return nullptr;
  }
  if (ae::Registry::GetRegistry().GenerationDistance(T::kClassId,
                                                     obj->GetClassId()) < 0) {
    return nullptr;
  }
  return static_cast<T const*>(obj);
}

Presenter* AsPresenter(ae::Obj* obj) { return AsObjOf<Presenter>(obj); }

ui::UiDesktopWindow* AsUiDesktopWindow(ae::Obj* obj) {
  return AsObjOf<ui::UiDesktopWindow>(obj);
}

void ApplyUiDesktopFrameRequestPresentation(ae::Obj& gui_root,
                                           ae::ObjId window_id) {
  std::vector<ae::Obj*> objects;
  CollectLiveReachableObjects(gui_root, objects);
  auto& registry = ae::Registry::GetRegistry();
  for (ae::Obj* obj : objects) {
    Presenter* const presenter = AsPresenter(obj);
    if (presenter == nullptr) {
      continue;
    }
    if (registry.GenerationDistance(ui::UiDesktopWindowPresenter::kClassId,
                                    presenter->GetClassId()) < 0) {
      continue;
    }
    auto* desktop_presenter =
        static_cast<ui::UiDesktopWindowPresenter*>(presenter);
    auto const window = desktop_presenter->desktop_window();
    if (!window.is_valid() || window->obj_id != window_id) {
      continue;
    }
    desktop_presenter->ApplyRequestedFrameFromMirror();
    return;
  }
}

void SaveObjectGraphToScratch(ae::Obj const& object,
                              ae::RamDomainStorage& scratch,
                              ae::GraphSerializationScope scope) {
  ae::Domain scratch_domain{scratch};
  ae::DomainGraph graph{&scratch_domain, scope};
  auto ptr = object.domain->Find(object.obj_id);
  assert(ptr);
  auto* factory = ae::Registry::GetRegistry().FindFactory(object.GetClassId());
  assert(factory != nullptr);
  assert(factory->save != nullptr);
  factory->save(&graph, ptr, object.obj_id);
}

void SaveObjectGraphToScratch(ae::Obj const& object,
                              ae::RamDomainStorage& scratch) {
  SaveObjectGraphToScratch(object, scratch,
                           ae::GraphSerializationScope::LocalPersistent);
}

bool InjectObjectBytes(ae::IDomainStorage& storage, ae::DomainQuery const& query,
                       std::uint8_t const* data, std::size_t size) {
  auto writer = storage.Store(query);
  if (!writer) {
    return false;
  }
  auto const result = writer->Write(ae::seri::DataWriteTag{data, size});
  return static_cast<bool>(result);
}

void InjectClassDataLayers(ae::RamDomainStorage::ClassData const& class_data,
                           ae::ObjId object_id, ae::IDomainStorage& storage) {
  for (auto const& [class_id, versions] : class_data) {
    for (auto const& [version, data] : versions) {
      if (!InjectObjectBytes(storage, {object_id, class_id, version},
                             data.data(), data.size())) {
        WriteFatalStderr("fatal: single-object layer inject failed\n");
        std::abort();
      }
    }
  }
}

void RemoveDistilledBaseObjects(std::vector<ae::Obj*>& objects) {
  std::unordered_set<std::uint32_t> base_ids;
  for (ae::Obj* obj : objects) {
    if (auto* node = AsObjOf<Node>(obj)) {
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

void RecordStorageLayers(ae::IDomainStorage& storage, ae::ObjId id,
                         InitialPublicationLoadDiagnostics* diagnostics) {
  if (diagnostics == nullptr) {
    return;
  }
  ae::ClassList const classes = storage.Enumerate(id);
  std::size_t pos = 0;
  for (std::size_t i = 0; i < classes.size() && pos + 12 < sizeof(diagnostics->storage_layers);
       ++i) {
    if (i > 0) {
      diagnostics->storage_layers[pos++] = ',';
    }
    int const written = std::snprintf(
        diagnostics->storage_layers + pos,
        sizeof(diagnostics->storage_layers) - pos, "%u", classes[i]);
    if (written <= 0) {
      break;
    }
    pos += static_cast<std::size_t>(written);
  }
}

bool LoadExistingObject(ae::Obj& object, ae::Domain& domain,
                        ae::IDomainStorage* storage_for_diag,
                        InitialPublicationLoadDiagnostics* diagnostics) {
  ae::DomainGraph graph{&domain};
  auto ptr = domain.Find(object.obj_id);
  if (!ptr) {
    if (diagnostics) {
      diagnostics->object_class_id = object.GetClassId();
    }
    return false;
  }
  auto* factory = ae::Registry::GetRegistry().FindFactory(object.GetClassId());
  if (factory == nullptr || factory->load == nullptr) {
    if (diagnostics) {
      diagnostics->object_class_id = object.GetClassId();
      diagnostics->factory_class_id = 0;
      if (storage_for_diag) {
        RecordStorageLayers(*storage_for_diag, object.obj_id, diagnostics);
      }
    }
    return false;
  }
  auto const load_result = factory->load(&graph, ptr, object.obj_id);
  if (load_result && load_result->IsErr()) {
    if (diagnostics) {
      diagnostics->object_class_id = object.GetClassId();
#ifndef NDEBUG
      diagnostics->factory_class_id = factory->cls_id;
#else
      diagnostics->factory_class_id = object.GetClassId();
#endif
      diagnostics->load_error_code = load_result->error().error_code;
      if (storage_for_diag) {
        RecordStorageLayers(*storage_for_diag, object.obj_id, diagnostics);
      }
    }
    return false;
  }
  return true;
}

void LoadExistingObjectOrAbort(ae::Obj& object, ae::Domain& domain) {
  if (!LoadExistingObject(object, domain, nullptr, nullptr)) {
    WriteFatalStderr("fatal: single-object load failed\n");
    std::abort();
  }
}

std::unordered_set<std::uint32_t> CollectDistilledBaseObjectIds(
    ae::Obj const& root, ae::RamDomainStorage::ObjClassData const& scratch_state) {
  std::unordered_set<std::uint32_t> distilled_base_ids;
  for (auto const& [obj_id, class_map_opt] : scratch_state) {
    if (!class_map_opt) {
      continue;
    }
    auto obj = root.domain->Find(obj_id);
    if (!obj) {
      continue;
    }
    if (auto* node = AsObjOf<Node>(obj.get())) {
      if (node->base.is_valid()) {
        distilled_base_ids.insert(node->base.id().id());
      }
    }
  }
  return distilled_base_ids;
}

GraphPublicationPayload BuildGraphPublicationPayload(
    ae::Obj const& root, ae::RamDomainStorage const& scratch,
    std::unordered_set<std::uint32_t> const& distilled_base_ids) {
  GraphPublicationPayload payload;
  for (auto const& [obj_id, class_map_opt] : scratch.state) {
    if (!class_map_opt) {
      continue;
    }
    if (distilled_base_ids.count(obj_id.id()) != 0) {
      continue;
    }
    payload.layers[obj_id] = *class_map_opt;
    auto obj = root.domain->Find(obj_id);
    if (!obj) {
      continue;
    }
    auto* node = AsObjOf<Node>(obj.get());
    if (node == nullptr) {
      continue;
    }
    payload.generations.push_back(
        GraphPublicationNodeGeneration{obj_id.id(), node->Generation()});
  }
  return payload;
}

bool SaveGraphPublicationPayload(GraphPublicationPayload const& payload,
                                 ByteSink& out) {
  ae::seri::BinaryVectorBuffer buffer{out.bytes};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  return archive.Save(payload).IsOk();
}

bool LoadGraphPublicationPayload(ByteSource& in, GraphPublicationPayload& payload) {
  if (in.pos > in.size) {
    return false;
  }
  std::vector<std::uint8_t> copy(in.data + in.pos, in.data + in.size);
  ae::seri::BinaryVectorBuffer buffer{copy};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  if (archive.Load(payload).IsErr()) {
    return false;
  }
  in.pos = in.size;
  return true;
}

bool InjectGraphPublicationLayers(
    GraphPublicationPayload const& payload, ae::IDomainStorage& storage,
    std::vector<std::uint32_t>& unique_obj_ids,
    InitialPublicationLoadDiagnostics* diagnostics) {
  unique_obj_ids.clear();
  std::uint32_t layer_index = 0;
  for (auto const& [obj_id, class_map_opt] : payload.layers) {
    if (!class_map_opt) {
      continue;
    }
    for (auto const& [class_id, versions] : *class_map_opt) {
      for (auto const& [version, data] : versions) {
        if (!InjectObjectBytes(storage, {obj_id, class_id, version},
                               data.data(), data.size())) {
          if (diagnostics) {
            diagnostics->stage = InitialPublicationLoadStage::InjectLayerBytes;
            diagnostics->layer_index = layer_index;
            diagnostics->obj_id = obj_id.id();
            diagnostics->class_id = class_id;
            diagnostics->version = version;
            diagnostics->size = static_cast<std::uint32_t>(data.size());
          }
          return false;
        }
        ++layer_index;
        auto const oid = obj_id.id();
        if (std::find(unique_obj_ids.begin(), unique_obj_ids.end(), oid) ==
            unique_obj_ids.end()) {
          unique_obj_ids.push_back(oid);
        }
      }
    }
  }
  if (diagnostics) {
    diagnostics->layer_count = layer_index;
  }
  return true;
}

void ApplyGraphPublicationNodeGenerations(GraphPublicationPayload const& payload,
                                          ae::Domain& domain) {
  for (GraphPublicationNodeGeneration const& entry : payload.generations) {
    auto object = domain.Find(ae::ObjId{entry.object_id});
    if (!object) {
      continue;
    }
    FinalizeUiNodeState(*object, entry.generation);
  }
}

bool ApplyGraphPublicationPayloadToUiDomain(
    GraphPublicationPayload const& payload, ae::Obj& existing_root,
    ae::Domain& domain, ae::IDomainStorage& domain_storage) {
  std::vector<std::uint32_t> object_ids;
  if (!InjectGraphPublicationLayers(payload, domain_storage, object_ids,
                                    nullptr)) {
    return false;
  }
  // Re-read the injected layers into the live mirror instances. Refs to live
  // objects resolve to the same instances; objects new to the mirror are
  // built by those refs. Objects the mirror has not loaded stay lazy.
  if (!LoadExistingObject(existing_root, domain, nullptr, nullptr)) {
    return false;
  }
  for (std::uint32_t const id : object_ids) {
    if (auto object = domain.Find(ae::ObjId{id}); object) {
      if (!LoadExistingObject(*object, domain, nullptr, nullptr)) {
        return false;
      }
    }
  }
  ApplyGraphPublicationNodeGenerations(payload, domain);
  return true;
}

}  // namespace

void SerializeObjectToBuffer(ae::Obj const& object, ByteSink& out,
                             ae::GraphSerializationScope scope) {
  ae::RamDomainStorage scratch;
  SaveObjectGraphToScratch(object, scratch, scope);
  auto const obj_it = scratch.state.find(object.obj_id);
  if (obj_it == scratch.state.end() || !obj_it->second.has_value()) {
    WriteFatalStderr("fatal: single-object publication save missing layers\n");
    std::abort();
  }
  ae::RamDomainStorage::ClassData const& class_data = *obj_it->second;
  ae::seri::BinaryVectorBuffer buffer{out.bytes};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  if (!archive.Save(class_data).IsOk()) {
    WriteFatalStderr("fatal: single-object ClassData save failed\n");
    std::abort();
  }
}

void SerializeObjectToBuffer(ae::Obj const& object, ByteSink& out) {
  SerializeObjectToBuffer(object, out,
                          ae::GraphSerializationScope::LocalPersistent);
}

void DeserializeObjectFromBuffer(ae::Obj& object, ByteSource& in,
                                 ae::Domain& domain,
                                 ae::IDomainStorage& domain_storage) {
  if (in.pos > in.size) {
    WriteFatalStderr("fatal: single-object publication truncated\n");
    std::abort();
  }
  std::vector<std::uint8_t> copy(in.data + in.pos, in.data + in.size);
  ae::seri::BinaryVectorBuffer buffer{copy};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  ae::RamDomainStorage::ClassData class_data;
  if (archive.Load(class_data).IsErr()) {
    WriteFatalStderr("fatal: single-object ClassData load failed\n");
    std::abort();
  }
  in.pos += archive.buffer().read_position();
  InjectClassDataLayers(class_data, object.obj_id, domain_storage);
  LoadExistingObjectOrAbort(object, domain);
}

void DeserializeObjectGraphFromBuffer(ae::Obj& existing_root, ByteSource& in,
                                      ae::Domain& domain,
                                      ae::IDomainStorage& domain_storage) {
  GraphPublicationPayload payload;
  if (!LoadGraphPublicationPayload(in, payload)) {
    WriteFatalStderr("fatal: graph publication payload load failed\n");
    std::abort();
  }
  if (!ApplyGraphPublicationPayloadToUiDomain(payload, existing_root, domain,
                                              domain_storage)) {
    WriteFatalStderr("fatal: graph publication payload apply failed\n");
    std::abort();
  }
}

void RegisterLiveGraphEdgeAppender(LiveGraphEdgeAppender appender) {
  g_live_graph_edge_appender = appender;
}

void CollectReachableObjectsFromLiveFields(ae::Obj& root,
                                           std::vector<ae::Obj*>& out) {
  std::unordered_set<std::uint32_t> seen;
  std::vector<ae::Obj*> stack;
  stack.push_back(&root);
  out.clear();
  while (!stack.empty()) {
    ae::Obj* obj = stack.back();
    stack.pop_back();
    if (!seen.insert(obj->obj_id.id()).second) {
      continue;
    }
    out.push_back(obj);
    ForEachGraphEdgeObjPtrOn(*obj, [&](auto& edge) {
      if (!edge.is_valid()) {
        return;
      }
      if (auto found = root.domain->Find(edge.id())) {
        stack.push_back(&*found);
      }
    });
    if (g_live_graph_edge_appender != nullptr) {
      g_live_graph_edge_appender(*obj, root, stack);
    }
  }
  RemoveDistilledBaseObjects(out);
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
    if (auto* node = AsObjOf<Node>(obj)) {
      out.push_back(node);
    }
  }
}

void CollectLiveReachableNodes(ae::Obj& root, std::vector<Node*>& out) {
  std::vector<ae::Obj*> objects;
  CollectReachableObjectsFromLiveFields(root, objects);
  out.clear();
  out.reserve(objects.size());
  for (ae::Obj* obj : objects) {
    if (auto* node = AsObjOf<Node>(obj)) {
      out.push_back(node);
    }
  }
}

void CollectPersistentSerializationReachableObjects(
    ae::Obj& root, std::vector<ae::Obj*>& out) {
  ae::RamDomainStorage scratch;
  SaveObjectGraphToScratch(root, scratch);

  std::unordered_set<std::uint32_t> seen;
  out.clear();
  out.reserve(scratch.state.size() + 1);
  auto add_id = [&](ae::ObjId const& id) {
    if (!id.is_valid()) {
      return;
    }
    if (!seen.insert(id.id()).second) {
      return;
    }
    if (auto obj = root.domain->Find(id)) {
      out.push_back(&*obj);
    }
  };
  add_id(root.obj_id);
  for (auto const& [obj_id, class_map_opt] : scratch.state) {
    (void)class_map_opt;
    add_id(obj_id);
  }
}

std::set<ae::ObjId> CollectPersistedSerializationObjectIds(ae::Obj& root) {
  ae::RamDomainStorage scratch;
  SaveObjectGraphToScratch(root, scratch);
  std::set<ae::ObjId> ids;
  for (auto const& [obj_id, class_map_opt] : scratch.state) {
    if (class_map_opt) {
      ids.insert(obj_id);
    }
  }
  return ids;
}

void SavePersistedObjectGraphToRam(ae::Obj& root, ae::RamDomainStorage& out) {
  SaveObjectGraphToScratch(root, out);
}

struct SavedNodeBookkeeping {
  Node* node;
  Node::ptr base;
  std::vector<EventRecord> journal;
};

void ClearReachableNodeHistoryForPublicationSave(ae::Obj const& root,
                                                 std::vector<SavedNodeBookkeeping>& saved) {
  std::vector<Node*> nodes;
  CollectReachableNodes(const_cast<ae::Obj&>(root), nodes);
  saved.clear();
  saved.reserve(nodes.size());
  for (Node* node : nodes) {
    saved.push_back(SavedNodeBookkeeping{node, node->base, std::move(node->journal)});
    node->base = Node::ptr{};
    node->journal.clear();
  }
}

void RestoreReachableNodeHistory(std::vector<SavedNodeBookkeeping>& saved) {
  for (SavedNodeBookkeeping& entry : saved) {
    entry.node->base = std::move(entry.base);
    entry.node->journal = std::move(entry.journal);
  }
  saved.clear();
}

void SerializeObjectGraphToBuffer(ae::Obj const& root, ByteSink& out) {
  ResetPublicationSerializationStats();
  ae::RamDomainStorage scratch;
  SaveObjectGraphToScratch(
      root, scratch, ae::GraphSerializationScope::MaterializedPublication);
  std::uint64_t layer_bytes = 0;
  for (auto const& [obj_id, class_map_opt] : scratch.state) {
    if (!class_map_opt) {
      continue;
    }
    for (auto const& [class_id, versions] : *class_map_opt) {
      for (auto const& [version, data] : versions) {
        layer_bytes += data.size();
      }
    }
  }
  detail::NoteMaterializedScratchObjectCount(scratch.state.size());
  ActivePublicationSerializationStats().materialized_scratch_layer_bytes =
      layer_bytes;
  ActivePublicationSerializationStats().materialized_historical_event_objects = 0;

  auto const distilled_base_ids =
      CollectDistilledBaseObjectIds(root, scratch.state);
  GraphPublicationPayload const payload =
      BuildGraphPublicationPayload(root, scratch, distilled_base_ids);
  if (!SaveGraphPublicationPayload(payload, out)) {
    WriteFatalStderr("fatal: graph publication payload save failed\n");
    std::abort();
  }
}

void CollectMaterializedReachableObjects(ae::Obj& root,
                                         std::vector<ae::Obj*>& out) {
  ae::RamDomainStorage scratch;
  SaveObjectGraphToScratch(
      root, scratch, ae::GraphSerializationScope::MaterializedPublication);
  detail::NoteMaterializedScratchObjectCount(scratch.state.size());
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

void CollectLiveReachableObjects(ae::Obj& root, std::vector<ae::Obj*>& out) {
  ResetPublicationSerializationStats();
  CollectReachableObjectsFromLiveFields(root, out);
  std::unordered_set<std::uint32_t> seen;
  seen.reserve(out.size() + 16);
  for (ae::Obj* obj : out) {
    seen.insert(obj->obj_id.id());
  }

  ae::RamDomainStorage scratch;
  SaveObjectGraphToScratch(
      root, scratch, ae::GraphSerializationScope::MaterializedPublication);
  detail::NoteMaterializedScratchObjectCount(scratch.state.size());
  out.reserve(out.size() + scratch.state.size());
  for (auto const& [obj_id, class_map_opt] : scratch.state) {
    if (!class_map_opt) {
      continue;
    }
    if (!seen.insert(obj_id.id()).second) {
      continue;
    }
    if (auto obj = root.domain->Find(obj_id)) {
      out.push_back(&*obj);
    }
  }
  RemoveDistilledBaseObjects(out);
}

Node* FindLiveReachableNode(ae::Obj& root, std::uint32_t object_id) {
  std::vector<ae::Obj*> live;
  CollectLiveReachableObjects(root, live);
  for (ae::Obj* obj : live) {
    if (obj->obj_id.id() == object_id) {
      return AsObjOf<Node>(obj);
    }
  }
  return nullptr;
}

void FinalizeUiNodeState(ae::Obj& object, std::uint64_t generation) {
  if (auto* node = AsObjOf<Node>(&object)) {
    node->AdoptPublishedGeneration(generation);
    node->base = Node::ptr{};
    node->journal.clear();
  }
}

void SerializeIncrementalNodePublication(Node const& node, ByteSink& out) {
  ByteSink payload;
  SerializeObjectToBuffer(node, payload,
                          ae::GraphSerializationScope::MaterializedPublication);
  IncrementalPublicationEnvelope envelope;
  envelope.object_id = node.obj_id.id();
  envelope.generation = node.Generation();
  envelope.object_bytes = std::move(payload.bytes);
  ae::seri::BinaryVectorBuffer buffer{out.bytes};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  if (!archive.Save(envelope).IsOk()) {
    WriteFatalStderr("fatal: incremental publication envelope save failed\n");
    std::abort();
  }
}

ae::Obj& ApplyIncrementalPublication(ByteSource& in, ae::Domain& domain,
                                     ae::IDomainStorage& storage) {
  if (in.pos > in.size) {
    WriteFatalStderr("fatal: incremental publication envelope truncated\n");
    std::abort();
  }
  std::vector<std::uint8_t> copy(in.data + in.pos, in.data + in.size);
  ae::seri::BinaryVectorBuffer buffer{copy};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  IncrementalPublicationEnvelope envelope;
  if (archive.Load(envelope).IsErr()) {
    WriteFatalStderr("fatal: incremental publication envelope load failed\n");
    std::abort();
  }
  in.pos += archive.buffer().read_position();
  auto object = domain.Find(ae::ObjId{envelope.object_id});
  if (!object) {
    WriteFatalStderr(
        "fatal: incremental publication object must already exist\n");
    std::abort();
  }
  ByteSource payload;
  payload.data = envelope.object_bytes.data();
  payload.size = envelope.object_bytes.size();
  payload.pos = 0;
  DeserializeObjectFromBuffer(*object, payload, domain, storage);
  FinalizeUiNodeState(*object, envelope.generation);
  return *object;
}

void SerializeStructuralNodePublication(Node const& node, ByteSink& out) {
  StructuralPublicationEnvelope envelope;
  envelope.object_id = node.obj_id.id();
  envelope.generation = node.Generation();
  ByteSink graph;
  SerializeObjectGraphToBuffer(node, graph);
  envelope.graph_bytes = std::move(graph.bytes);
  envelope.sidecar = kStructuralPublicationSidecarNone;
  if (ui::UiDesktopWindow* desktop =
          AsUiDesktopWindow(const_cast<Node*>(&node))) {
    if (desktop->TakeFrameRequestPendingPublication()) {
      envelope.sidecar = kStructuralPublicationSidecarUiDesktopFrameRequest;
    }
  }
  out.bytes.clear();
  ae::seri::BinaryVectorBuffer buffer{out.bytes};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  if (!archive.Save(envelope).IsOk()) {
    WriteFatalStderr(
        "fatal: structural publication envelope save failed\n");
    std::abort();
  }
}

ae::Obj& ApplyStructuralPublication(ByteSource& in, ae::Domain& domain,
                                    ae::IDomainStorage& storage,
                                    std::uint8_t* sidecar_out) {
  if (in.pos > in.size) {
    WriteFatalStderr("fatal: structural publication envelope truncated\n");
    std::abort();
  }
  std::vector<std::uint8_t> const envelope_bytes(
      in.data + in.pos, in.data + in.size);
  std::vector<std::uint8_t> copy = envelope_bytes;
  ae::seri::BinaryVectorBuffer buffer{copy};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  StructuralPublicationEnvelope envelope{};
  if (archive.Load(envelope).IsErr()) {
    WriteFatalStderr("fatal: structural publication envelope load failed\n");
    std::abort();
  }
  in.pos = in.size;

  auto object = domain.Find(ae::ObjId{envelope.object_id});
  assert(object && "structural publication object must already exist");
  ByteSource graph;
  graph.data = envelope.graph_bytes.data();
  graph.size = envelope.graph_bytes.size();
  graph.pos = 0;
  DeserializeObjectGraphFromBuffer(*object, graph, domain, storage);
  FinalizeUiNodeState(*object, envelope.generation);
  if (sidecar_out != nullptr) {
    *sidecar_out = envelope.sidecar;
  }
  return *object;
}

bool InitialPublicationPayloadContainsObject(
    std::vector<std::uint8_t> const& initial_publication_bytes,
    ae::ObjId object_id) {
  if (initial_publication_bytes.empty()) {
    return false;
  }
  std::vector<std::uint8_t> copy = initial_publication_bytes;
  ae::seri::BinaryVectorBuffer buffer{copy};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  InitialPublicationEnvelope envelope{};
  if (archive.Load(envelope).IsErr()) {
    return false;
  }
  ByteSource graph;
  graph.data = envelope.graph_bytes.data();
  graph.size = envelope.graph_bytes.size();
  graph.pos = 0;
  GraphPublicationPayload payload;
  if (!LoadGraphPublicationPayload(graph, payload)) {
    return false;
  }
  auto const obj_it = payload.layers.find(object_id);
  return obj_it != payload.layers.end() && static_cast<bool>(obj_it->second);
}

bool InitialPublicationPayloadHasLayer(
    std::vector<std::uint8_t> const& initial_publication_bytes,
    ae::ObjId object_id, std::uint32_t class_id) {
  if (initial_publication_bytes.empty()) {
    return false;
  }
  std::vector<std::uint8_t> copy = initial_publication_bytes;
  ae::seri::BinaryVectorBuffer buffer{copy};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  InitialPublicationEnvelope envelope{};
  if (archive.Load(envelope).IsErr()) {
    return false;
  }
  ByteSource graph;
  graph.data = envelope.graph_bytes.data();
  graph.size = envelope.graph_bytes.size();
  graph.pos = 0;
  GraphPublicationPayload payload;
  if (!LoadGraphPublicationPayload(graph, payload)) {
    return false;
  }
  auto const obj_it = payload.layers.find(object_id);
  if (obj_it == payload.layers.end() || !obj_it->second) {
    return false;
  }
  auto const class_it = obj_it->second->find(class_id);
  if (class_it == obj_it->second->end() || class_it->second.empty()) {
    return false;
  }
  for (auto const& [version, data] : class_it->second) {
    if (!data.empty()) {
      return true;
    }
  }
  return false;
}

bool InitialPublicationPayloadCopyLayerBytes(
    std::vector<std::uint8_t> const& initial_publication_bytes,
    ae::ObjId object_id, std::uint32_t class_id, std::uint8_t version,
    std::vector<std::uint8_t>& out_layer_bytes) {
  out_layer_bytes.clear();
  if (initial_publication_bytes.empty()) {
    return false;
  }
  std::vector<std::uint8_t> copy = initial_publication_bytes;
  ae::seri::BinaryVectorBuffer buffer{copy};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  InitialPublicationEnvelope envelope{};
  if (archive.Load(envelope).IsErr()) {
    return false;
  }
  ByteSource graph;
  graph.data = envelope.graph_bytes.data();
  graph.size = envelope.graph_bytes.size();
  graph.pos = 0;
  GraphPublicationPayload payload;
  if (!LoadGraphPublicationPayload(graph, payload)) {
    return false;
  }
  auto const obj_it = payload.layers.find(object_id);
  if (obj_it == payload.layers.end() || !obj_it->second) {
    return false;
  }
  auto const class_it = obj_it->second->find(class_id);
  if (class_it == obj_it->second->end()) {
    return false;
  }
  auto const version_it = class_it->second.find(version);
  if (version_it == class_it->second.end() || version_it->second.empty()) {
    return false;
  }
  out_layer_bytes = version_it->second;
  return true;
}

bool InitialPublicationPayloadReplaceLayerBytes(
    std::vector<std::uint8_t>& initial_publication_bytes, ae::ObjId object_id,
    std::uint32_t class_id, std::uint8_t version,
    std::vector<std::uint8_t> const& new_layer_bytes) {
  if (initial_publication_bytes.empty()) {
    return false;
  }
  ae::seri::BinaryVectorBuffer buffer{initial_publication_bytes};
  ae::seri::BinaryArchive load_archive{buffer};
  InitialPublicationEnvelope envelope{};
  if (load_archive.Load(envelope).IsErr()) {
    return false;
  }
  ByteSource graph;
  graph.data = envelope.graph_bytes.data();
  graph.size = envelope.graph_bytes.size();
  graph.pos = 0;
  GraphPublicationPayload payload;
  if (!LoadGraphPublicationPayload(graph, payload)) {
    return false;
  }
  auto obj_it = payload.layers.find(object_id);
  if (obj_it == payload.layers.end() || !obj_it->second) {
    return false;
  }
  auto class_it = obj_it->second->find(class_id);
  if (class_it == obj_it->second->end()) {
    return false;
  }
  class_it->second[version] = new_layer_bytes;

  std::vector<std::uint8_t> graph_bytes;
  ae::seri::BinaryVectorBuffer graph_buffer{graph_bytes};
  ae::seri::BinaryArchive save_graph{graph_buffer};
  if (save_graph.Save(payload).IsErr()) {
    return false;
  }
  envelope.graph_bytes = std::move(graph_bytes);

  initial_publication_bytes.clear();
  ae::seri::BinaryVectorBuffer out_buffer{initial_publication_bytes};
  ae::seri::BinaryArchive save_envelope{out_buffer};
  return save_envelope.Save(envelope).IsOk();
}

bool InitialPublicationPayloadRemoveObjectLayers(
    std::vector<std::uint8_t>& initial_publication_bytes, ae::ObjId object_id) {
  if (initial_publication_bytes.empty()) {
    return false;
  }
  ae::seri::BinaryVectorBuffer buffer{initial_publication_bytes};
  ae::seri::BinaryArchive load_archive{buffer};
  InitialPublicationEnvelope envelope{};
  if (load_archive.Load(envelope).IsErr()) {
    return false;
  }
  ByteSource graph;
  graph.data = envelope.graph_bytes.data();
  graph.size = envelope.graph_bytes.size();
  graph.pos = 0;
  GraphPublicationPayload payload;
  if (!LoadGraphPublicationPayload(graph, payload)) {
    return false;
  }
  auto obj_it = payload.layers.find(object_id);
  if (obj_it == payload.layers.end()) {
    return false;
  }
  payload.layers.erase(obj_it);

  std::vector<std::uint8_t> graph_bytes;
  ae::seri::BinaryVectorBuffer graph_buffer{graph_bytes};
  ae::seri::BinaryArchive save_graph{graph_buffer};
  if (save_graph.Save(payload).IsErr()) {
    return false;
  }
  envelope.graph_bytes = std::move(graph_bytes);

  initial_publication_bytes.clear();
  ae::seri::BinaryVectorBuffer out_buffer{initial_publication_bytes};
  ae::seri::BinaryArchive save_envelope{out_buffer};
  return save_envelope.Save(envelope).IsOk();
}

void SerializeInitialPublication(ae::Obj const& root, ByteSink& out) {
  std::vector<Node*> nodes;
  CollectReachableNodes(const_cast<ae::Obj&>(root), nodes);
  for (Node* node : nodes) {
    node->EnsureCurrentGeneration();
  }
  InitialPublicationEnvelope envelope;
  envelope.root_id = root.obj_id.id();
  ByteSink graph;
  SerializeObjectGraphToBuffer(root, graph);
  envelope.graph_bytes = std::move(graph.bytes);
  out.bytes.clear();
  ae::seri::BinaryVectorBuffer buffer{out.bytes};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  if (!archive.Save(envelope).IsOk()) {
    WriteFatalStderr("fatal: initial publication envelope save failed\n");
    std::abort();
  }
}

void FormatInitialPublicationLoadDiagnostics(InitialPublicationLoadDiagnostics const& diagnostics,
                                             char* buffer, std::size_t buffer_size) {
  char const* stage = "unknown";
  switch (diagnostics.stage) {
    case InitialPublicationLoadStage::Ok:
      stage = "ok";
      break;
    case InitialPublicationLoadStage::ReadRootId:
      stage = "read_root_id";
      break;
    case InitialPublicationLoadStage::ReadEnvelope:
      stage = "read_envelope";
      break;
    case InitialPublicationLoadStage::ReadGraphPayload:
      stage = "read_graph_payload";
      break;
    case InitialPublicationLoadStage::ReadLayerCount:
      stage = "read_layer_count";
      break;
    case InitialPublicationLoadStage::ReadLayerHeader:
      stage = "read_layer_header";
      break;
    case InitialPublicationLoadStage::ReadLayerPayload:
      stage = "read_layer_payload";
      break;
    case InitialPublicationLoadStage::InjectLayerBytes:
      stage = "inject_layer_bytes";
      break;
    case InitialPublicationLoadStage::LoadRoot:
      stage = "load_root";
      break;
    case InitialPublicationLoadStage::ReadGenerationCount:
      stage = "read_generation_count";
      break;
    case InitialPublicationLoadStage::ReadGenerationEntry:
      stage = "read_generation_entry";
      break;
  }
  std::snprintf(
      buffer, buffer_size,
      "InitialPublicationLoad failed stage=%s root_id=%u obj_id=%u class_id=%u version=%u "
      "offset=%u size=%u layer_index=%u layer_count=%u object_class_id=%u "
      "factory_class_id=%u load_error_code=%d storage_layers=%s\n",
      stage, diagnostics.root_id, diagnostics.obj_id, diagnostics.class_id,
      static_cast<unsigned>(diagnostics.version), diagnostics.offset, diagnostics.size,
      diagnostics.layer_index, diagnostics.layer_count, diagnostics.object_class_id,
      diagnostics.factory_class_id, diagnostics.load_error_code,
      diagnostics.storage_layers[0] != '\0' ? diagnostics.storage_layers : "(none)");
}

ae::Ptr<ae::Obj> TryLoadInitialPublication(ByteSource& in, ae::Domain& ui_domain,
                                          ae::IDomainStorage& ui_storage,
                                          InitialPublicationLoadDiagnostics* diagnostics_out) {
  InitialPublicationLoadDiagnostics local;
  InitialPublicationLoadDiagnostics* diagnostics = diagnostics_out ? diagnostics_out : &local;
  *diagnostics = {};

  if (in.pos > in.size) {
    diagnostics->stage = InitialPublicationLoadStage::ReadEnvelope;
    diagnostics->offset = static_cast<std::uint32_t>(in.pos);
    return {};
  }
  std::vector<std::uint8_t> const envelope_bytes(in.data + in.pos,
                                                 in.data + in.size);
  std::vector<std::uint8_t> copy = envelope_bytes;
  ae::seri::BinaryVectorBuffer buffer{copy};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  InitialPublicationEnvelope envelope{};
  if (archive.Load(envelope).IsErr()) {
    diagnostics->stage = InitialPublicationLoadStage::ReadEnvelope;
    diagnostics->offset = static_cast<std::uint32_t>(in.pos);
    return {};
  }
  in.pos = in.size;

  std::uint32_t const root_id = envelope.root_id;
  diagnostics->root_id = root_id;

  ByteSource graph;
  graph.data = envelope.graph_bytes.data();
  graph.size = envelope.graph_bytes.size();
  graph.pos = 0;

  GraphPublicationPayload payload;
  if (!LoadGraphPublicationPayload(graph, payload)) {
    diagnostics->stage = InitialPublicationLoadStage::ReadGraphPayload;
    diagnostics->offset = 0;
    return {};
  }

  std::vector<std::uint32_t> unique_obj_ids;
  if (!InjectGraphPublicationLayers(payload, ui_storage, unique_obj_ids,
                                    diagnostics)) {
    return {};
  }

  // One LoadRoot builds every eagerly referenced object; ui_root owns the
  // graph from here on, Domain only holds weak entries. Lazy refs stay unloaded.
  ae::DomainGraph load_graph{&ui_domain};
  ae::Ptr<ae::Obj> ui_root;
  auto const load_result = load_graph.LoadRoot(ae::ObjId{root_id}, ui_root);
  if (load_result.IsErr()) {
    diagnostics->stage = InitialPublicationLoadStage::LoadRoot;
    diagnostics->load_error_code = load_result.error().error_code;
    RecordStorageLayers(ui_storage, ae::ObjId{root_id}, diagnostics);
    return {};
  }

  ApplyGraphPublicationNodeGenerations(payload, ui_domain);

  diagnostics->stage = InitialPublicationLoadStage::Ok;
  return ui_root;
}

ae::Ptr<ae::Obj> LoadInitialPublication(ByteSource& in, ae::Domain& ui_domain,
                                      ae::IDomainStorage& ui_storage) {
  InitialPublicationLoadDiagnostics diagnostics;
  auto ui_root = TryLoadInitialPublication(in, ui_domain, ui_storage, &diagnostics);
  if (!ui_root) {
    char message[512];
    FormatInitialPublicationLoadDiagnostics(diagnostics, message, sizeof(message));
    WriteFatalStderr(message);
    std::abort();
  }
  return ui_root;
}

namespace {

std::uint64_t NextPresentationLoadOrder() {
  static std::atomic<std::uint64_t> next{1};
  return next.fetch_add(1, std::memory_order_relaxed);
}

void UnloadPresenter(Presenter& presenter) {
  assert(presenter.presentation_loaded);
  presenter.OnUnload();
  presenter.presentation_loaded = false;
  presenter.presentation_load_order = 0;
}

void UnloadPresentersDescending(
    std::vector<Presenter*> presenters) {
  std::sort(presenters.begin(), presenters.end(),
            [](Presenter* a, Presenter* b) {
              return a->presentation_load_order > b->presentation_load_order;
            });
  for (Presenter* presenter : presenters) {
    if (presenter == nullptr || !presenter->presentation_loaded) {
      continue;
    }
    UnloadPresenter(*presenter);
  }
}

}  // namespace

void InitializeNewPresenters(ae::Obj& gui_root, void* host,
                             ModelObjectProxy* model_proxy) {
  // Multipass: child presenters may wait until a parent HWND exists.
  for (;;) {
    bool progress = false;
    std::vector<ae::Obj*> objects;
    CollectLiveReachableObjects(gui_root, objects);
    for (ae::Obj* obj : objects) {
      Presenter* const presenter = AsPresenter(obj);
      if (presenter == nullptr || presenter->presentation_loaded) {
        continue;
      }
      presenter->presentation_host = host;
      presenter->model_proxy = model_proxy;
      if (!presenter->ReadyForPresentation()) {
        continue;
      }
      presenter->OnLoad();
      presenter->presentation_loaded = true;
      presenter->presentation_load_order = NextPresentationLoadOrder();
      progress = true;
    }
    if (!progress) {
      break;
    }
  }

  // Every live Presenter must have native presentation after multipass.
  // Remaining not-ready means a broken graph / missing parent dependency.
  std::vector<ae::Obj*> objects;
  CollectLiveReachableObjects(gui_root, objects);
  for (ae::Obj* obj : objects) {
    Presenter* const presenter = AsPresenter(obj);
    if (presenter == nullptr) {
      continue;
    }
    if (!presenter->presentation_loaded) {
      char message[192];
      std::snprintf(
          message, sizeof(message),
          "fatal: live presenter obj_id=%u class_id=%u not activated after "
          "multipass\n",
          presenter->obj_id.id(), presenter->GetClassId());
      WriteFatalStderr(message);
      std::abort();
    }
  }
}

void InitializePresenters(ae::Obj& gui_root, void* host,
                          ModelObjectProxy* model_proxy) {
  InitializeNewPresenters(gui_root, host, model_proxy);
  std::vector<ae::Obj*> objects;
  CollectLiveReachableObjects(gui_root, objects);
  for (ae::Obj* obj : objects) {
    Presenter* const presenter = AsPresenter(obj);
    if (presenter != nullptr && presenter->presentation_loaded) {
      presenter->OnModelChanged();
    }
  }
}

void UnloadPresenters(ae::Obj& gui_root) {
  std::vector<ae::Obj*> objects;
  CollectLiveReachableObjects(gui_root, objects);
  std::vector<Presenter*> loaded;
  for (ae::Obj* obj : objects) {
    Presenter* const presenter = AsPresenter(obj);
    if (presenter != nullptr && presenter->presentation_loaded) {
      loaded.push_back(presenter);
    }
  }
  UnloadPresentersDescending(std::move(loaded));
}

void UpdatePresentersAfterStructuralPublication(
    ae::Obj& gui_root, std::vector<Presenter::ptr> const& previously_active,
    void* host, ModelObjectProxy* model_proxy) {
  std::vector<ae::Obj*> live_objects;
  CollectLiveReachableObjects(gui_root, live_objects);
  std::unordered_set<Presenter*> live_presenters;
  for (ae::Obj* obj : live_objects) {
    if (Presenter* const presenter = AsPresenter(obj)) {
      live_presenters.insert(presenter);
    }
  }
  std::vector<Presenter*> removed;
  for (Presenter::ptr const& held : previously_active) {
    if (!held.is_valid() || !held.is_loaded()) {
      continue;
    }
    Presenter* presenter = &*held;
    if (!presenter->presentation_loaded) {
      continue;
    }
    if (live_presenters.count(presenter) != 0) {
      continue;
    }
    removed.push_back(presenter);
  }
  UnloadPresentersDescending(std::move(removed));
  InitializeNewPresenters(gui_root, host, model_proxy);
  for (ae::Obj* obj : live_objects) {
    Presenter* const presenter = AsPresenter(obj);
    if (presenter != nullptr && presenter->presentation_loaded) {
      presenter->OnModelChanged();
    }
  }
}

StructuralPresentationKeepalive CaptureStructuralPresentationKeepalive(
    ae::Obj& gui_root) {
  StructuralPresentationKeepalive keepalive;
  std::vector<ae::Obj*> objects;
  CollectLiveReachableObjects(gui_root, objects);
  ae::Domain* const domain = gui_root.domain;
  assert(domain != nullptr && "GUI root must belong to a Domain");
  keepalive.live_objects.reserve(objects.size());
  keepalive.active_presenters.reserve(objects.size());
  for (ae::Obj* obj : objects) {
    // Domain::Find returns the canonical Ptr for the most-derived storage.
    // MakePtrFromThis / ObjPtr::MakeFromThis on a base pointer is UB when the
    // object was allocated as a derived type; wasm hits that as
    // BuildDecrementGraphImpl OOB when keepalive tears down after Apply.
    auto held = domain->Find(obj->obj_id);
    assert(held && "live reachable object must remain in Domain");
    keepalive.live_objects.push_back(std::move(held));
    Presenter* const presenter = AsPresenter(obj);
    if (presenter != nullptr && presenter->presentation_loaded) {
      // Keep the Domain::Find Ptr as Obj вЂ” do not re-wrap through
      // Ptr<Presenter>{Ptr<Obj>} with a separate static_cast path. ObjPtr
      // stores Ptr<Obj> in ObjectPtrBase; Presenter::ptr from Find is enough
      // for OnUnload while the keepalive still owns the Obj.
      auto presenter_held = domain->Find(presenter->obj_id);
      assert(presenter_held && "loaded presenter must remain in Domain");
      keepalive.active_presenters.push_back(Presenter::ptr{
          domain, presenter->obj_id, {},
          std::move(presenter_held)});
    }
  }
  return keepalive;
}

ae::Obj& ApplyStructuralPublicationAndUpdatePresenters(
    ByteSource& in, ae::Domain& domain, ae::IDomainStorage& storage,
    ae::Obj& gui_root, void* host, ModelObjectProxy* model_proxy) {
  StructuralPresentationKeepalive keepalive =
      CaptureStructuralPresentationKeepalive(gui_root);
  std::uint8_t sidecar = kStructuralPublicationSidecarNone;
  ae::Obj& changed =
      ApplyStructuralPublication(in, domain, storage, &sidecar);
  if (sidecar == kStructuralPublicationSidecarUiDesktopFrameRequest) {
    ApplyUiDesktopFrameRequestPresentation(gui_root, changed.obj_id);
  }
  UpdatePresentersAfterStructuralPublication(
      gui_root, keepalive.active_presenters, host, model_proxy);
  // Release presenter anchors before live_objects so derived Presenter::ptr
  // teardown cannot race Obj graph decrement during wasm Ptr GC.
  keepalive.active_presenters.clear();
  keepalive.live_objects.clear();
  return changed;
}
}  // namespace apptraverse
