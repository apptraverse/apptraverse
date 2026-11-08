#include "window_catalog_main_native_frame_queue.h"

#include <cassert>

#include "window_catalog_model.h"

#include "apptraverse/ui/ui_desktop_window.h"

namespace apptraverse {

void ModelObjectProxy::BindMainNativeFrameQueue(
    WindowCatalogMainNativeFrameQueue* queue) {
  assert(queue != nullptr);
  main_native_frame_queue_ = queue;
}

void ModelObjectProxy::UnbindMainNativeFrameQueue(
    WindowCatalogMainNativeFrameQueue* queue) {
  if (main_native_frame_queue_ == queue) {
    main_native_frame_queue_ = nullptr;
  }
}

WindowCatalogMainNativeFrameQueue* ModelObjectProxy::MainNativeFrameQueue()
    const {
  return main_native_frame_queue_;
}

namespace {

struct MainFrameApply {
  ae::ObjId catalog_id;
  ae::ObjId main_window_id;
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t client_width{0};
  std::int32_t client_height{0};
  bool updates_restore_geometry{false};
};

void ApplyCoalescedMainNativeFrame(ae::Domain& domain,
                                   MainFrameApply const& slot) {
  auto window_obj = domain.Find(slot.main_window_id);
  assert(window_obj && "main window native frame target must exist");
  auto* window = window_obj.as<ui::UiDesktopWindow>();
  window->SubmitFrameFromNative(slot.x, slot.y, slot.client_width,
                                slot.client_height,
                                slot.updates_restore_geometry);
  auto catalog_obj = domain.Find(slot.catalog_id);
  assert(catalog_obj && "catalog viewport target must exist");
  auto* catalog = catalog_obj.as<WindowCatalog>();
  catalog->SubmitMainClientViewportFromUi(slot.client_width, slot.client_height,
                                          160);
}

}  // namespace

void WindowCatalogMainNativeFrameQueue::BindPostWork(PostWork post) {
  std::lock_guard<std::mutex> lock{mu_};
  posting_closed_ = false;
  post_ = std::move(post);
}

void WindowCatalogMainNativeFrameQueue::ClosePosting() {
  std::lock_guard<std::mutex> lock{mu_};
  posting_closed_ = true;
}

void WindowCatalogMainNativeFrameQueue::BindProxy(ModelObjectProxy* proxy) {
  assert(proxy != nullptr);
  proxy->BindMainNativeFrameQueue(this);
}

void WindowCatalogMainNativeFrameQueue::UnbindProxy(ModelObjectProxy* proxy) {
  assert(proxy != nullptr);
  proxy->UnbindMainNativeFrameQueue(this);
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
  post_([this](ae::Domain& domain) {
    MainFrameApply slot{};
    bool need_again = false;
    {
      std::lock_guard<std::mutex> lock{mu_};
      drain_scheduled_ = false;
      if (!slot_active_) {
        return;
      }
      slot.catalog_id = slot_.catalog_id;
      slot.main_window_id = slot_.main_window_id;
      slot.x = slot_.x;
      slot.y = slot_.y;
      slot.client_width = slot_.client_width;
      slot.client_height = slot_.client_height;
      slot.updates_restore_geometry = slot_.updates_restore_geometry;
    }
    ApplyCoalescedMainNativeFrame(domain, slot);
    {
      std::lock_guard<std::mutex> lock{mu_};
      if (!slot_active_) {
        return;
      }
      if (slot_.catalog_id != slot.catalog_id ||
          slot_.main_window_id != slot.main_window_id ||
          slot_.x != slot.x || slot_.y != slot.y ||
          slot_.client_width != slot.client_width ||
          slot_.client_height != slot.client_height ||
          slot_.updates_restore_geometry != slot.updates_restore_geometry) {
        need_again = true;
      } else {
        slot_active_ = false;
      }
    }
    if (need_again) {
      EnsureDrainScheduled();
    }
  });
}

void WindowCatalogMainNativeFrameQueue::Post(ModelObjectProxy* proxy,
                                             ae::ObjId catalog_id,
                                             ae::ObjId main_window_id,
                                             std::int32_t x, std::int32_t y,
                                             std::int32_t client_width,
                                             std::int32_t client_height,
                                             bool updates_restore_geometry) {
  assert(proxy != nullptr);
  assert(proxy->MainNativeFrameQueue() == this);
  {
    std::lock_guard<std::mutex> lock{mu_};
    assert(post_);
    assert(!posting_closed_);
    slot_active_ = true;
    slot_.catalog_id = catalog_id;
    slot_.main_window_id = main_window_id;
    slot_.x = x;
    slot_.y = y;
    slot_.client_width = client_width;
    slot_.client_height = client_height;
    slot_.updates_restore_geometry = updates_restore_geometry;
  }
  EnsureDrainScheduled();
}

void PostWindowCatalogMainNativeFrame(ModelObjectProxy* proxy,
                                      ae::ObjId catalog_id,
                                      ae::ObjId main_window_id,
                                      std::int32_t x, std::int32_t y,
                                      std::int32_t client_width,
                                      std::int32_t client_height,
                                      bool updates_restore_geometry) {
  assert(proxy != nullptr);
  WindowCatalogMainNativeFrameQueue* queue = proxy->MainNativeFrameQueue();
  assert(queue != nullptr);
  queue->Post(proxy, catalog_id, main_window_id, x, y, client_width,
              client_height, updates_restore_geometry);
}

}  // namespace apptraverse
