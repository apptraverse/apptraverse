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
  std::deque<ModelObjectProxy::ModelWork> posted;
  SharedWindowCatalogMainNativeFrameQueue().BindPostWork(
      [&](ModelObjectProxy::ModelWork work) {
        posted.push_back(std::move(work));
      });
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) {
    posted.push_back(std::move(work));
  }};
  ae::ObjId const catalog_id{1};
  ae::ObjId const window_id{2};
  for (int i = 0; i < 64; ++i) {
    PostWindowCatalogMainNativeFrame(&proxy, catalog_id, window_id, 10, 20,
                                     500 + i, 400, false);
  }
  CHECK(posted.size() == 1);
}

void TestUncoalescedFallbackQueuesTwoWorksPerPost() {
  WindowCatalogMainNativeFrameQueue fresh;
  CHECK(!fresh.post_bound());
  std::deque<ModelObjectProxy::ModelWork> posted;
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) {
    posted.push_back(std::move(work));
  }};
  ae::ObjId const catalog_id{1};
  ae::ObjId const window_id{2};
  for (int i = 0; i < 8; ++i) {
    PostWindowCatalogMainNativeFrame(&proxy, catalog_id, window_id, 0, 0, 640,
                                     480, false);
  }
  CHECK(posted.size() == 16);
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

  std::deque<ModelObjectProxy::ModelWork> posted;
  SharedWindowCatalogMainNativeFrameQueue().BindPostWork(
      [&](ModelObjectProxy::ModelWork work) {
        posted.push_back(std::move(work));
      });
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork) {}};
  PostWindowCatalogMainNativeFrame(&proxy, catalog_id, main_id, 80, 80, 520, 880,
                                   false);
  CHECK(posted.size() == 1);
  posted.front()(domain);
  CHECK(catalog->persisted.main_window->client_width() == 520);
  CHECK(catalog->persisted.main_client_viewport.width == 520);
  CHECK(catalog->persisted.main_client_viewport.height == 880);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::test::TestUncoalescedFallbackQueuesTwoWorksPerPost();
  apptraverse::test::TestCoalescedQueuePostsSingleModelWork();
  apptraverse::test::TestCoalescedDrainAppliesFrameAndViewportTogether();
  std::cout << "window_catalog_main_native_frame_coalesce_test OK\n";
  return 0;
}
