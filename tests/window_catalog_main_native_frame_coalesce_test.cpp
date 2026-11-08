#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/application_model_session.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/runtime_node.h"

#include "window_catalog_graph.h"
#include "window_catalog_ids.h"
#include "window_catalog_main_native_frame_queue.h"
#include "window_catalog_model.h"
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

struct PublicationWaiter {
  std::mutex mu;
  std::condition_variable cv;
  int pending{0};
  ApplicationPublicationKind kind{ApplicationPublicationKind::Initial};
};

void WaitPublication(PublicationWaiter& waiter) {
  std::unique_lock<std::mutex> lock{waiter.mu};
  waiter.cv.wait(lock, [&] { return waiter.pending > 0; });
  --waiter.pending;
}

void TestCoalescedQueuePostsSingleModelWork() {
  WindowCatalogMainNativeFrameQueue queue;
  std::deque<ModelObjectProxy::ModelWork> posted;
  queue.BindPostWork([&](ModelObjectProxy::ModelWork work) {
    posted.push_back(std::move(work));
  });
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) {
    posted.push_back(std::move(work));
  }};
  ae::ObjId const catalog_id{1};
  ae::ObjId const window_id{2};
  for (int i = 0; i < 64; ++i) {
    PostWindowCatalogMainNativeFrame(queue, &proxy, catalog_id, window_id, 10,
                                     20, 500 + i, 400, false);
  }
  CHECK(posted.size() == 1);
}

void TestIndependentSessionsApplyDistinctFrames() {
  EnsureObjectRegistration();
  EnsureWindowCatalogGraphRegistration();

  ae::RamDomainStorage storage_a;
  ae::RamDomainStorage storage_b;
  ae::Domain domain_a{storage_a};
  ae::Domain domain_b{storage_b};
  auto catalog_a = BuildWindowCatalogGraph(domain_a);
  auto catalog_b = BuildWindowCatalogGraph(domain_b);
  PrepareWindowCatalogForRun(*catalog_a);
  PrepareWindowCatalogForRun(*catalog_b);
  ae::ObjId const catalog_id = catalog_a->obj_id;
  ae::ObjId const window_id_a = catalog_a->persisted.main_window.id();
  ae::ObjId const window_id_b = catalog_b->persisted.main_window.id();
  CHECK(window_id_a == window_id_b);

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
  ModelObjectProxy proxy_a{[&](ModelObjectProxy::ModelWork) {}};
  ModelObjectProxy proxy_b{[&](ModelObjectProxy::ModelWork) {}};

  for (int i = 0; i < 8; ++i) {
    PostWindowCatalogMainNativeFrame(queue_a, &proxy_a, catalog_id, window_id_a,
                                     0, 0, 640 + i, 480, false);
    PostWindowCatalogMainNativeFrame(queue_b, &proxy_b, catalog_id, window_id_b,
                                     0, 0, 320 + i, 900, false);
  }
  CHECK(posted_a.size() == 1);
  CHECK(posted_b.size() == 1);
  posted_a.front()(domain_a);
  posted_b.front()(domain_b);
  CHECK(catalog_a->persisted.main_window->client_width() == 647);
  CHECK(catalog_a->persisted.main_client_viewport.height == 480);
  CHECK(catalog_b->persisted.main_window->client_width() == 327);
  CHECK(catalog_b->persisted.main_client_viewport.height == 900);
  queue_a.ClosePosting();
  queue_b.ClosePosting();
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
  PostWindowCatalogMainNativeFrame(queue, &proxy, catalog_id, main_id, 80, 80,
                                   520, 880, false);
  CHECK(posted.size() == 1);
  posted.front()(domain);
  CHECK(catalog->persisted.main_window->client_width() == 520);
  CHECK(catalog->persisted.main_client_viewport.width == 520);
  CHECK(catalog->persisted.main_client_viewport.height == 880);
  queue.ClosePosting();
}

void TestShutdownRuntimeDrainsPendingResizeBeforePersist() {
  EnsureObjectRegistration();
  EnsureWindowCatalogGraphRegistration();

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto catalog = BuildWindowCatalogGraph(domain);
  PrepareWindowCatalogForRun(*catalog);
  ae::ObjId const catalog_id = catalog->obj_id;
  ae::ObjId const main_window_id = catalog->persisted.main_window.id();

  ApplicationModelRuntime runtime;
  WindowCatalogMainNativeFrameQueue queue;
  queue.BindPostWork([&](ModelObjectProxy::ModelWork work) {
    runtime.Post(std::move(work));
  });
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) {
    runtime.Post(std::move(work));
  }};

  std::mutex hold_mu;
  std::condition_variable hold_cv;
  bool release_model = false;
  std::atomic<bool> hold_running{false};

  std::thread model_thread{[&] {
    runtime.Run(
        domain, *catalog,
        [&](ApplicationPublicationKind kind) {
          if (kind == ApplicationPublicationKind::Initial) {
            runtime.TakePublishedCopy();
            runtime.NotifyPublicationConsumed();
          }
        },
        {});
  }};

  runtime.Post([&](ae::Domain&) {
    hold_running.store(true);
    std::unique_lock<std::mutex> lock{hold_mu};
    hold_cv.wait(lock, [&] { return release_model; });
  });
  while (!hold_running.load()) {
    std::this_thread::yield();
  }

  PostWindowCatalogMainNativeFrame(queue, &proxy, catalog_id, main_window_id, 12,
                                   34, 812, 610, false);
  proxy.Invoke(catalog_id, &WindowCatalog::AddWindow);
  runtime.RequestStop();
  release_model = true;
  hold_cv.notify_all();
  model_thread.join();
  queue.ClosePosting();

  catalog->persisted.main_window.Load();
  CHECK(catalog->persisted.main_window->client_width() == 812);
  CHECK(catalog->persisted.main_client_viewport.width == 812);
  CHECK(catalog->persisted.main_client_viewport.height == 610);
  catalog->persisted.list.Load();
  CHECK(catalog->persisted.list->rows.size() == 1);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::test::TestIndependentSessionsApplyDistinctFrames();
  apptraverse::test::TestCoalescedQueuePostsSingleModelWork();
  apptraverse::test::TestCoalescedDrainAppliesFrameAndViewportTogether();
  apptraverse::test::TestShutdownRuntimeDrainsPendingResizeBeforePersist();
  std::cout << "window_catalog_main_native_frame_coalesce_test OK\n";
  return 0;
}
