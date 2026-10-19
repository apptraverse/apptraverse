#ifndef APPTRAVERSE_SURFACES_PRESENTATION_SIZE_QUEUE_H_
#define APPTRAVERSE_SURFACES_PRESENTATION_SIZE_QUEUE_H_

#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>

#include "aether-objects/obj/obj_id.h"

#include "apptraverse/model_object_proxy.h"

namespace apptraverse {

// Coalesces GUI-thread presentation size reports per Surface before model
// Commit. At most one drain task is queued on the model thread at a time; each
// pass applies the latest coalesced size per surface and reschedules if newer
// sizes arrived during Apply.
class SurfacesPresentationSizeQueue {
 public:
  using PostWork = std::function<void(ModelObjectProxy::ModelWork)>;

  void BindPostWork(PostWork post);
  void Post(ModelObjectProxy* proxy, ae::ObjId surface_id, std::int32_t width,
            std::int32_t height);

  void BindProxy(ModelObjectProxy* proxy);
  void UnbindProxy(ModelObjectProxy* proxy);

  [[nodiscard]] std::size_t PendingSurfaceCountForTest() const;

 private:
  struct SurfaceSlot {
    std::int32_t width{0};
    std::int32_t height{0};
  };

  void EnsureDrainScheduled();

  mutable std::mutex mu_;
  PostWork post_;
  bool drain_scheduled_{false};
  std::unordered_map<std::uint32_t, SurfaceSlot> slots_;
  std::unordered_map<ModelObjectProxy*, int> proxy_refs_;
};

void PostSurfacePresentationSize(ModelObjectProxy* proxy, ae::ObjId surface_id,
                                 std::int32_t width, std::int32_t height);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_PRESENTATION_SIZE_QUEUE_H_
