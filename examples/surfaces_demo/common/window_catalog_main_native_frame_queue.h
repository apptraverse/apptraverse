#ifndef APPTRAVERSE_WINDOW_CATALOG_MAIN_NATIVE_FRAME_QUEUE_H_
#define APPTRAVERSE_WINDOW_CATALOG_MAIN_NATIVE_FRAME_QUEUE_H_

#include <cstdint>
#include <functional>
#include <mutex>

#include "aether-objects/obj/obj_id.h"

#include "apptraverse/model_object_proxy.h"

namespace apptraverse {

// Coalesces main-window native frame + catalog viewport reports before model
// Commit. One drain task per catalog at a time; the slot keeps the latest
// native client geometry. Owned by WindowCatalogModelSession; bound to the
// session ModelObjectProxy for the lifetime of UI presenters.
class WindowCatalogMainNativeFrameQueue {
 public:
  using PostWork = std::function<void(ModelObjectProxy::ModelWork)>;

  void BindPostWork(PostWork post);
  void ClosePosting();

  void Post(ModelObjectProxy* proxy, ae::ObjId catalog_id,
            ae::ObjId main_window_id, std::int32_t x, std::int32_t y,
            std::int32_t client_width, std::int32_t client_height,
            bool updates_restore_geometry);

 private:
  struct CatalogSlot {
    ae::ObjId catalog_id;
    ae::ObjId main_window_id;
    std::int32_t x{0};
    std::int32_t y{0};
    std::int32_t client_width{0};
    std::int32_t client_height{0};
    bool updates_restore_geometry{false};
  };

  void EnsureDrainScheduled();

  mutable std::mutex mu_;
  PostWork post_;
  bool posting_closed_{false};
  bool drain_scheduled_{false};
  CatalogSlot slot_{};
  bool slot_active_{false};
};

void PostWindowCatalogMainNativeFrame(
    WindowCatalogMainNativeFrameQueue& queue, ModelObjectProxy* proxy,
    ae::ObjId catalog_id, ae::ObjId main_window_id, std::int32_t x,
    std::int32_t y, std::int32_t client_width, std::int32_t client_height,
    bool updates_restore_geometry);

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_MAIN_NATIVE_FRAME_QUEUE_H_
