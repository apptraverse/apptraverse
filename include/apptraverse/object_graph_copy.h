#ifndef APPTRAVERSE_OBJECT_GRAPH_COPY_H_
#define APPTRAVERSE_OBJECT_GRAPH_COPY_H_

#include <cstdint>
#include <set>

#include "aether/obj/domain.h"
#include "aether/obj/idomain_storage.h"
#include "aether/obj/obj.h"
#include "aether/obj/obj_id.h"

namespace apptraverse {

// Application synchronization endpoint. Works with any IDomainStorage.
struct SyncReplica {
  ae::Domain& domain;
  ae::IDomainStorage& storage;
  ae::ObjId shared_root_id;
};

bool StorageHasObject(ae::IDomainStorage& storage, ae::ObjId id);
bool StorageHasClass(ae::IDomainStorage& storage, ae::ObjId id,
                     std::uint32_t class_id);

// How SharedPtr targets are treated when copying into a destination storage.
enum class SharedCopyMode {
  // Encode / full materialize: keep SharedPtr loaded; copy missing targets.
  kCopyLoadedTargets,
  // Import into application: existing SharedPtr targets become reference-only;
  // missing targets are copied then stored as unloaded references.
  kReferenceExistingTargets,
};

namespace detail {

struct PrepareSyncGraphContext {
  ae::IDomainStorage* dest_for_refs{nullptr};
  SharedCopyMode mode{SharedCopyMode::kCopyLoadedTargets};
  std::set<ae::ObjId::Type> visiting_nodes;
};

}  // namespace detail

// Deep-copy the loaded object graph rooted at `source` into `target_domain` /
// `target_storage`, preserving ObjIds. Never mutates the source graph.
// LocalPtr edges are cleared in a scratch copy before save (targets omitted).
void CopyObjectGraph(ae::Obj::ptr source, ae::IDomainStorage& source_storage,
                     ae::Domain& target_domain,
                     ae::IDomainStorage& target_storage, SharedCopyMode mode);

// Import a decoded graph into the application replica. Existing object records
// are not replaced. Returns a pointer to the root loaded in the target Domain.
ae::Obj::ptr ImportObjectGraph(ae::Obj::ptr source,
                               ae::IDomainStorage& source_storage,
                               SyncReplica& target, SharedCopyMode mode);

}  // namespace apptraverse

#endif  // APPTRAVERSE_OBJECT_GRAPH_COPY_H_
