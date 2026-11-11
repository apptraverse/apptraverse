#include "window_catalog_main_native_frame_queue.h"

#include <cassert>

#include "window_catalog_model.h"

#include "apptraverse/ui/ui_desktop_window.h"

namespace apptraverse {
void WindowCatalogMainNativeFrameQueue::BindPostWork(PostWork post) {
  std::lock_guard<std::mutex> lock{mu_};
  posting_closed_ = false;
  post_ = std::move(post);
}

void WindowCatalogMainNativeFrameQueue::ClosePosting() {
  std::lock_guard<std::mutex> lock{mu_};
  posting_closed_ = true;
}

void WindowCatalogMainNativeFrameQueue::EnsureDrainScheduled() {
  {
    std::lock_guard<std::mutex> lock{mu_};
    assert(post_);
    if (drain_scheduled_) {
      return;
    }
    drain_scheduled_ = true;
  }
  post_([this](ae::Domain& domain) { Drain(domain); });
}

void WindowCatalogMainNativeFrameQueue::Drain(ae::Domain& domain) {
  for (;;) {
    CatalogSlot slot;
    {
      std::lock_guard<std::mutex> lock{mu_};
      if (!slot_active_) {
        drain_scheduled_ = false;
        return;
      }
      slot = slot_;
      slot_active_ = false;
      slot_.restore_frame.reset();
    }

    auto window_obj = domain.Find(slot.main_window_id);
    assert(window_obj && "main window native frame target must exist");
    auto* window = window_obj.as<ui::UiDesktopWindow>();
    // A later live report (for example maximization or WM_CLOSE) must not
    // erase the last completed move/resize or lend it different coordinates.
    if (slot.restore_frame) {
      auto const& restore = *slot.restore_frame;
      window->SubmitFrameFromNative(restore.x, restore.y,
                                    restore.client_width,
                                    restore.client_height, true);
    }
    auto const& live = slot.live_frame;
    window->SubmitFrameFromNative(live.x, live.y, live.client_width,
                                  live.client_height, false);
    auto catalog_obj = domain.Find(slot.catalog_id);
    assert(catalog_obj && "catalog viewport target must exist");
    auto* catalog = catalog_obj.as<WindowCatalog>();
    catalog->SubmitMainClientViewportFromUi(live.client_width,
                                            live.client_height, 160);

    // Keep this task scheduled while applying outside the lock. Reports that
    // arrive during Apply belong to the next snapshot of this same drain,
    // including when the runtime has already stopped accepting new tasks.
  }
}

void WindowCatalogMainNativeFrameQueue::Post(ModelObjectProxy* proxy,
                                             ae::ObjId catalog_id,
                                             ae::ObjId main_window_id,
                                             std::int32_t x, std::int32_t y,
                                             std::int32_t client_width,
                                             std::int32_t client_height,
                                             bool updates_restore_geometry) {
  assert(proxy != nullptr);
  {
    std::lock_guard<std::mutex> lock{mu_};
    assert(post_);
    assert(!posting_closed_);
    slot_active_ = true;
    slot_.catalog_id = catalog_id;
    slot_.main_window_id = main_window_id;
    slot_.live_frame = NativeFrame{x, y, client_width, client_height};
    if (updates_restore_geometry) {
      slot_.restore_frame = slot_.live_frame;
    }
  }
  EnsureDrainScheduled();
}

void PostWindowCatalogMainNativeFrame(
    WindowCatalogMainNativeFrameQueue& queue, ModelObjectProxy* proxy,
    ae::ObjId catalog_id, ae::ObjId main_window_id, std::int32_t x,
    std::int32_t y, std::int32_t client_width, std::int32_t client_height,
    bool updates_restore_geometry) {
  assert(proxy != nullptr);
  queue.Post(proxy, catalog_id, main_window_id, x, y, client_width,
             client_height, updates_restore_geometry);
}

}  // namespace apptraverse
