#include <cstdlib>
#include <deque>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/runtime_node.h"

#include "window_catalog_graph.h"
#include "window_catalog_main_native_frame_queue.h"
#include "window_catalog_prepare.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (false)

void TestCoalescedQueuePostsSingleModelWork() {
  WindowCatalogMainNativeFrameQueue queue;
  std::deque<ModelObjectProxy::ModelWork> posted;
  queue.BindPostWork([&](ModelObjectProxy::ModelWork work) {
    posted.push_back(std::move(work));
  });
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) {
    posted.push_back(std::move(work));
  }};
  queue.BindProxy(&proxy);
  ae::ObjId const catalog_id{1};
  ae::ObjId const window_id{2};
  for (int i = 0; i < 64; ++i) {
    PostWindowCatalogMainNativeFrame(&proxy, catalog_id, window_id, 10, 20,
                                     500 + i, 400, false);
  }
  CHECK(posted.size() == 1);
  queue.UnbindProxy(&proxy);
}

void TestIndependentSessionsDoNotMixCoalescedPosts() {
  WindowCatalogMainNativeFrameQueue queue_a;
  WindowCatalogMainNativeFrameQueue queue_b;
  std::deque<ModelObjectProxy::ModelWork> posted_a;
  std::deque<ModelObjectProxy::ModelWork> posted_b;
  queue_a.BindPostWork([&](ModelObjectProxy::ModelWork work) {
    posted_a.push_back(std::move(work));
  });
  queue_b.BindPostWork([&](ModelObjectProxy::ModelWork work) {
    posted_b.push_back(std::move(work));
  });
  ModelObjectProxy proxy_a{[&](ModelObjectProxy::ModelWork work) {
    posted_a.push_back(std::move(work));
  }};
  ModelObjectProxy proxy_b{[&](ModelObjectProxy::ModelWork work) {
    posted_b.push_back(std::move(work));
  }};
  queue_a.BindProxy(&proxy_a);
  queue_b.BindProxy(&proxy_b);
  ae::ObjId const catalog_id{1};
  ae::ObjId const window_id{2};
  for (int i = 0; i < 8; ++i) {
    PostWindowCatalogMainNativeFrame(&proxy_a, catalog_id, window_id, 0, 0,
                                     640 + i, 480, false);
    PostWindowCatalogMainNativeFrame(&proxy_b, catalog_id, window_id, 0, 0,
                                     320 + i, 900, false);
  }
  CHECK(posted_a.size() == 1);
  CHECK(posted_b.size() == 1);
  queue_a.UnbindProxy(&proxy_a);
  queue_b.UnbindProxy(&proxy_b);
}

void TestCoalescedDrainAppliesFrameAndViewportTogether() {
  EnsureObjectRegistration();
  EnsureWindowCatalogGraphRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto catalog = BuildWindowCatalogGraph(domain);
  PrepareWindowCatalogForRun(*catalog);
  ae::ObjId const catalog_id = catalog->obj_id;
  ae::ObjId const main_id = catalog->persisted.main_window.id();

  WindowCatalogMainNativeFrameQueue queue;
  std::deque<ModelObjectProxy::ModelWork> posted;
  queue.BindPostWork([&](ModelObjectProxy::ModelWork work) {
    posted.push_back(std::move(work));
  });
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork) {}};
  queue.BindProxy(&proxy);
  PostWindowCatalogMainNativeFrame(&proxy, catalog_id, main_id, 80, 80, 520, 880,
                                   false);
  CHECK(posted.size() == 1);
  posted.front()(domain);
  CHECK(catalog->persisted.main_window->client_width() == 520);
  CHECK(catalog->persisted.main_client_viewport.width == 520);
  CHECK(catalog->persisted.main_client_viewport.height == 880);
  queue.ClosePosting();
  queue.UnbindProxy(&proxy);
}

void TestShutdownCompletesPendingResizeBeforeClosePosting() {
  EnsureObjectRegistration();
  EnsureWindowCatalogGraphRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto catalog = BuildWindowCatalogGraph(domain);
  PrepareWindowCatalogForRun(*catalog);
  ae::ObjId const catalog_id = catalog->obj_id;
  ae::ObjId const main_id = catalog->persisted.main_window.id();

  WindowCatalogMainNativeFrameQueue queue;
  std::deque<ModelObjectProxy::ModelWork> posted;
  queue.BindPostWork([&](ModelObjectProxy::ModelWork work) {
    posted.push_back(std::move(work));
  });
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork) {}};
  queue.BindProxy(&proxy);
  PostWindowCatalogMainNativeFrame(&proxy, catalog_id, main_id, 0, 0, 700, 500,
                                   false);
  CHECK(posted.size() == 1);
  posted.front()(domain);
  CHECK(catalog->persisted.main_window->client_width() == 700);
  queue.ClosePosting();
  queue.UnbindProxy(&proxy);
  CHECK(proxy.MainNativeFrameQueue() == nullptr);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::test::TestIndependentSessionsDoNotMixCoalescedPosts();
  apptraverse::test::TestCoalescedQueuePostsSingleModelWork();
  apptraverse::test::TestCoalescedDrainAppliesFrameAndViewportTogether();
  apptraverse::test::TestShutdownCompletesPendingResizeBeforeClosePosting();
  std::cout << "window_catalog_main_native_frame_coalesce_test OK\n";
  return 0;
}
