#include "window_catalog_main_native_frame_queue.h"

#include <cassert>

#include "window_catalog_model.h"

#include "apptraverse/ui/ui_desktop_window.h"

namespace apptraverse {
namespace {

WindowCatalogMainNativeFrameQueue g_shared_queue;

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
  if (!window_obj) {
    return;
  }
  auto* window = window_obj.as<ui::UiDesktopWindow>();
  window->SubmitFrameFromNative(slot.x, slot.y, slot.client_width,
                                slot.client_height,
                                slot.updates_restore_geometry);
  auto catalog_obj = domain.Find(slot.catalog_id);
  if (!catalog_obj) {
    return;
  }
  auto* catalog = catalog_obj.as<WindowCatalog>();
  catalog->SubmitMainClientViewportFromUi(slot.client_width, slot.client_height,
                                          160);
}

}  // namespace

void WindowCatalogMainNativeFrameQueue::BindPostWork(PostWork post) {
  std::lock_guard<std::mutex> lock{mu_};
  post_ = std::move(post);
  drain_scheduled_ = false;
  slot_active_ = false;
}

bool WindowCatalogMainNativeFrameQueue::post_bound() const {
  std::lock_guard<std::mutex> lock{mu_};
  return static_cast<bool>(post_);
}

std::size_t WindowCatalogMainNativeFrameQueue::pending_catalog_count_for_test()
    const {
  std::lock_guard<std::mutex> lock{mu_};
  return slot_active_ ? 1U : 0U;
}

bool WindowCatalogMainNativeFrameQueue::drain_scheduled_for_test() const {
  std::lock_guard<std::mutex> lock{mu_};
  return drain_scheduled_;
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
  (void)proxy;
  {
    std::lock_guard<std::mutex> lock{mu_};
    assert(post_);
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

WindowCatalogMainNativeFrameQueue& SharedWindowCatalogMainNativeFrameQueue() {
  return g_shared_queue;
}

void PostWindowCatalogMainNativeFrame(ModelObjectProxy* proxy,
                                      ae::ObjId catalog_id,
                                      ae::ObjId main_window_id,
                                      std::int32_t x, std::int32_t y,
                                      std::int32_t client_width,
                                      std::int32_t client_height,
                                      bool updates_restore_geometry) {
  assert(proxy != nullptr);
  WindowCatalogMainNativeFrameQueue& queue =
      SharedWindowCatalogMainNativeFrameQueue();
  if (queue.post_bound()) {
    queue.Post(proxy, catalog_id, main_window_id, x, y, client_width,
               client_height, updates_restore_geometry);
    return;
  }
  proxy->Invoke(main_window_id, &ui::UiDesktopWindow::SubmitFrameFromNative, x,
                y, client_width, client_height, updates_restore_geometry);
  proxy->Invoke(catalog_id, &WindowCatalog::SubmitMainClientViewportFromUi,
                client_width, client_height, 160);
}

}  // namespace apptraverse
