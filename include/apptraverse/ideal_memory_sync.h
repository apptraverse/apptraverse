#ifndef APPTRAVERSE_IDEAL_MEMORY_SYNC_H_
#define APPTRAVERSE_IDEAL_MEMORY_SYNC_H_

#include <cstddef>

#include "apptraverse/object_state.h"

namespace apptraverse {

struct SyncResult {
  std::size_t nodes_imported{0};
  std::size_t events_imported{0};
};

SyncResult SynchronizeSharedGraphOneWay(MemoryReplica& source,
                                        MemoryReplica& target);

SyncResult SynchronizeSharedGraphBidirectional(MemoryReplica& left,
                                               MemoryReplica& right);

}  // namespace apptraverse

#endif  // APPTRAVERSE_IDEAL_MEMORY_SYNC_H_
