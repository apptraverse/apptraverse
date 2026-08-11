#include "apptraverse/sync_session_state.h"

#include <cassert>

namespace apptraverse {

SyncSessionState::ptr CreateSyncSessionState(ae::Domain& domain,
                                             ae::ObjId shared_root_id) {
  assert(shared_root_id.IsValid());
  auto base = SyncSessionState::ptr::Create(ae::CreateWith{domain});
  auto live = SyncSessionState::ptr::Create(ae::CreateWith{domain});
  live->base = base;
  live->data.shared_root_id = shared_root_id;
  live->CaptureBaseState();
  return live;
}

}  // namespace apptraverse
