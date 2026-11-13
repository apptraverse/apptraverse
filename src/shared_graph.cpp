#include "apptraverse/shared_graph.h"

#include <algorithm>
#include <utility>

#include "aether-miscpp/serialization/binary_archive.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_ptr_base.h"
#include "aether-objects/obj/registry.h"
#include "apptraverse/closed_event_graph.h"
#include "apptraverse/event.h"

namespace apptraverse {

bool CaptureSharedGraph(ae::Obj const& root, SharedGraphSnapshot& snapshot) {
  if (root.domain == nullptr) {
    return false;
  }
  ae::RamDomainStorage scratch;
  ae::Domain scratch_domain{scratch};
  ae::DomainGraph graph{&scratch_domain,
                        ae::GraphSerializationScope::NetworkShared};
  graph.force_materialize = true;
  auto const saved = graph.SaveRoot(root.domain->Find(root.obj_id), root.obj_id);
  if (saved.IsErr()) {
    return false;
  }
  snapshot = SharedGraphSnapshot{root.obj_id, std::move(scratch.state)};
  return true;
}

bool SerializeSharedGraph(ae::Obj const& root,
                          std::vector<std::uint8_t>& bytes) {
  SharedGraphSnapshot snapshot;
  if (!CaptureSharedGraph(root, snapshot)) {
    return false;
  }
  std::vector<std::uint8_t> frozen;
  ae::seri::BinaryVectorBuffer buffer{frozen};
  ae::seri::BinaryArchive archive{buffer};
  if (archive.Save(snapshot).IsErr()) {
    return false;
  }
  bytes = std::move(frozen);
  return true;
}

bool DeserializeSharedGraph(std::vector<std::uint8_t> const& bytes,
                            SharedGraphSnapshot& snapshot) {
  auto input = bytes;
  ae::seri::BinaryVectorBuffer buffer{input};
  ae::seri::BinaryArchive archive{buffer};
  SharedGraphSnapshot parsed;
  if (archive.Load(parsed).IsErr() ||
      archive.buffer().read_position() != input.size() ||
      !parsed.root_id.is_valid()) {
    return false;
  }
  ae::RamDomainStorage storage;
  storage.state = parsed.layers;
  if (!ValidateStoredClassChains(storage) ||
      storage.Enumerate(parsed.root_id).empty()) {
    return false;
  }
  snapshot = std::move(parsed);
  return true;
}

bool ValidateSharedGraphSnapshot(SharedGraphSnapshot const& snapshot,
                                  std::uint32_t expected_root_base_class_id) {
  if (!snapshot.root_id.is_valid()) {
    return false;
  }
  ae::RamDomainStorage parsed;
  parsed.state = snapshot.layers;
  std::vector<StoredClassChainInfo> chains;
  if (!ValidateStoredClassChains(parsed, &chains)) {
    return false;
  }
  auto const root = std::find_if(chains.begin(), chains.end(),
      [&](auto const& chain) { return chain.obj_id == snapshot.root_id; });
  if (root == chains.end()) {
    return false;
  }
  auto& registry = ae::Registry::GetRegistry();
  if (expected_root_base_class_id != 0 &&
      registry.GenerationDistance(expected_root_base_class_id,
                                    root->most_derived_class_id) < 0) {
    return false;
  }
  if (registry.GenerationDistance(Event::kClassId,
                                    root->most_derived_class_id) >= 0) {
    return ValidateClosedEventGraphStorage(parsed, snapshot.root_id);
  }

  // Native load validates every stored object, including lazy/non-root layers.
  // Hold all instances so validation never depends on a temporary pointer's
  // lifetime or the load order of an alias/cycle.
  ae::Domain source_domain{parsed};
  ae::DomainGraph load{&source_domain,
                        ae::GraphSerializationScope::NetworkShared};
  std::vector<ae::Ptr<ae::Obj>> objects;
  objects.reserve(chains.size());
  for (auto const& chain : chains) {
    ae::Ptr<ae::Obj> object;
    auto const loaded = load.LoadRoot(chain.obj_id, object);
    if (loaded.IsErr() || !object ||
        object->GetClassId() != chain.most_derived_class_id) {
      return false;
    }
    objects.push_back(std::move(object));
  }
  struct References {
    ae::RamDomainStorage const* parsed;
    bool closed{true};
  } references{&parsed};
  ae::RamDomainStorage checked;
  ae::Domain checked_domain{checked};
  ae::DomainGraph save{&checked_domain,
                        ae::GraphSerializationScope::NetworkShared};
  save.force_materialize = true;
  save.SetObjectReferenceVisitor(&references,
      +[](void* context, ae::ObjectPtrBase& pointer) {
        auto& refs = *static_cast<References*>(context);
        if (!pointer.is_valid()) {
          return;
        }
        auto const found = refs.parsed->state.find(pointer.id());
        if (found == refs.parsed->state.end() || !found->second.has_value()) {
          refs.closed = false;
        }
      });
  for (auto const& object : objects) {
    auto const saved = save.SaveRoot(object, object->obj_id);
    if (saved.IsErr() || !references.closed) {
      return false;
    }
  }
  return references.closed;
}

bool ImportSharedGraphNewObjects(SharedGraphSnapshot const& snapshot,
                                  ae::Domain& domain,
                                  ae::IDomainStorage& storage,
                                  ae::Ptr<ae::Obj>& out_root) {
  if (!ValidateSharedGraphSnapshot(snapshot)) {
    return false;
  }
  ae::RamDomainStorage parsed;
  parsed.state = snapshot.layers;
  std::vector<StoredClassChainInfo> chains;
  if (!ValidateStoredClassChains(parsed, &chains)) {
    return false;
  }

  ae::RamDomainStorage new_objects;
  auto& registry = ae::Registry::GetRegistry();
  for (auto const& chain : chains) {
    auto const existing = domain.Find(chain.obj_id);
    if (existing && existing->GetClassId() != chain.most_derived_class_id) {
      return false;
    }
    auto const stored_classes = storage.Enumerate(chain.obj_id);
    if (!stored_classes.empty()) {
      // A cold definition is just as authoritative as an already-loaded one.
      // In particular, an unregistered or more-derived local class is not
      // replaced with a compatible-looking base from a packet.
      bool has_most_derived = false;
      for (auto const class_id : stored_classes) {
        if (class_id == ae::Obj::kClassId) {
          continue;
        }
        if (!registry.IsExisting(class_id) ||
            registry.GenerationDistance(class_id,
                                          chain.most_derived_class_id) < 0) {
          return false;
        }
        has_most_derived = has_most_derived ||
                           class_id == chain.most_derived_class_id;
      }
      if (!has_most_derived) {
        return false;
      }
    }
    if (!existing && stored_classes.empty()) {
      new_objects.state.emplace(chain.obj_id,
                                 snapshot.layers.find(chain.obj_id)->second);
    }
  }

  // Layer transfer uses native storage writers; it does not re-encode fields.
  // No known object is rehydrated or written during this operation.
  for (auto const& [object_id, classes] : new_objects.state) {
    for (auto const& [class_id, versions] : *classes) {
      for (auto const& [version, bytes] : versions) {
        auto writer = storage.Store(ae::DomainQuery{object_id, class_id, version});
        if (writer == nullptr) {
          return false;
        }
        if (!bytes.empty()) {
          auto const written = writer->Write(
              ae::seri::DataWriteTag{bytes.data(), bytes.size()});
          if (written.IsErr()) {
            return false;
          }
        }
      }
    }
  }
  ae::DomainGraph load{&domain,
                        ae::GraphSerializationScope::NetworkShared};
  ae::Ptr<ae::Obj> root;
  auto const loaded = load.LoadRoot(snapshot.root_id, root);
  if (loaded.IsErr() || !root) {
    return false;
  }
  out_root = std::move(root);
  return true;
}

}  // namespace apptraverse
