#include "apptraverse/object_state.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include "aether/clock.h"

#include "apptraverse/object_state_transfer.h"

namespace apptraverse {
namespace {

void SortObjectState(ObjectState& state) {
  std::sort(state.objects.begin(), state.objects.end(),
            [](StoredObjectVersion const& a, StoredObjectVersion const& b) {
              if (a.obj_id != b.obj_id) {
                return a.obj_id < b.obj_id;
              }
              if (a.class_id != b.class_id) {
                return a.class_id < b.class_id;
              }
              return a.version < b.version;
            });
}

ObjectState ExtractOwnedRecords(ae::ObjId root_id,
                                ae::RamDomainStorage const& storage,
                                std::vector<ae::ObjId> const& owned_ids) {
  ObjectState state;
  state.root_id = root_id;
  for (auto const& obj_id : owned_ids) {
    auto const it = storage.state.find(obj_id);
    assert(it != storage.state.end());
    assert(it->second.has_value());
    for (auto const& [class_id, versions] : *it->second) {
      for (auto const& [version, data] : versions) {
        state.objects.push_back(StoredObjectVersion{
            obj_id,
            class_id,
            version,
            data,
        });
      }
    }
  }
  SortObjectState(state);
  return state;
}

template <typename Ptr>
ObjectState CaptureObjectStateFromPtr(Ptr root,
                                      ae::RamDomainStorage const& storage) {
  assert(root.is_valid());
  assert(root.is_loaded());
  assert(root.domain() != nullptr);

  // Persist source so the copied storage is current. Source links are not
  // mutated afterward.
  root.Save();

  ae::RamDomainStorage scratch_storage;
  scratch_storage.state = storage.state;

  ae::Domain scratch_domain{ae::Now(), scratch_storage};
  auto scratch_root =
      Ptr::Declare(ae::CreateWith{scratch_domain}.with_id(root.id()));
  scratch_root.Load();
  assert(scratch_root.is_loaded());

  detail::OwnedObjectIdCollector owned;
  owned.Add(scratch_root.id());
  scratch_root->PrepareScopedTransfer(owned);

  scratch_root.Save();

  return ExtractOwnedRecords(scratch_root.id(), scratch_storage, owned.ids);
}

}  // namespace

ObjectState CaptureNodeState(Node::ptr node,
                             ae::RamDomainStorage const& storage) {
  return CaptureObjectStateFromPtr(std::move(node), storage);
}

ObjectState CaptureEventState(Event::ptr event,
                              ae::RamDomainStorage const& storage) {
  return CaptureObjectStateFromPtr(std::move(event), storage);
}

void ImportObjectState(ObjectState const& state,
                       ae::RamDomainStorage& target_storage) {
  for (auto const& object : state.objects) {
    ae::ObjectData data = object.data;
    target_storage.SaveData(
        ae::DomainQuery{object.obj_id, object.class_id, object.version},
        std::move(data));
  }
}

bool TransferRemoteEvent(Event::ptr source_event,
                         std::uint64_t original_timestamp_us,
                         ae::RamDomainStorage const& source_storage,
                         Node::ptr target_node,
                         ae::RamDomainStorage& target_storage) {
  assert(source_event.is_valid());
  assert(source_event.is_loaded());
  assert(target_node.is_valid());
  assert(target_node.is_loaded());
  assert(target_node.domain() != nullptr);

  auto const event_id = source_event.id();
  auto state = CaptureEventState(source_event, source_storage);
  ImportObjectState(state, target_storage);

  auto imported = Event::ptr::Declare(
      ae::CreateWith{*target_node.domain()}.with_id(event_id));
  imported.Load();
  assert(imported.is_loaded());
  assert(imported.domain() == target_node.domain());

  bool const accepted =
      target_node->AcceptRemoteEvent(std::move(imported), original_timestamp_us);
  if (accepted) {
    target_node.Save();
  }
  return accepted;
}

}  // namespace apptraverse
