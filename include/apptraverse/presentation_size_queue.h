#ifndef APPTRAVERSE_PRESENTATION_SIZE_QUEUE_H_
#define APPTRAVERSE_PRESENTATION_SIZE_QUEUE_H_

#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>

#include "aether-objects/obj/obj_id.h"

#include "apptraverse/model_object_proxy.h"

namespace apptraverse {

// Coalesces GUI-thread presentation size reports per object before model
// Commit. At most one drain task is queued on the model thread at a time.
class PresentationSizeQueue {
 public:
  using PostWork = std::function<void(ModelObjectProxy::ModelWork)>;
  using SizeApplier =
      std::function<void(ae::Domain&, ae::ObjId, std::int32_t, std::int32_t)>;

  void BindPostWork(PostWork post);
  void BindSizeApplier(SizeApplier applier);

  void Post(ModelObjectProxy* proxy, ae::ObjId object_id, std::int32_t width,
            std::int32_t height);

  void BindProxy(ModelObjectProxy* proxy);
  void UnbindProxy(ModelObjectProxy* proxy);

  [[nodiscard]] std::size_t PendingObjectCountForTest() const;
  [[nodiscard]] bool DrainScheduledForTest() const;

 private:
  struct SizeSlot {
    std::int32_t width{0};
    std::int32_t height{0};
  };

  void EnsureDrainScheduled();

  mutable std::mutex mu_;
  PostWork post_;
  SizeApplier size_applier_;
  bool drain_scheduled_{false};
  std::unordered_map<std::uint32_t, SizeSlot> slots_;
  std::unordered_map<ModelObjectProxy*, int> proxy_refs_;
};

void PostPresentationSize(ModelObjectProxy* proxy, ae::ObjId object_id,
                          std::int32_t width, std::int32_t height);

}  // namespace apptraverse

#endif  // APPTRAVERSE_PRESENTATION_SIZE_QUEUE_H_
