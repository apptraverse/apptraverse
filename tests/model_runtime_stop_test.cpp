#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/model_runtime.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui_mirror.h"

namespace apptraverse::test {
namespace {

class Counter;
class BumpEvent;

class Counter : public NodeFor<Counter> {
  APPTRAVERSE_OBJECT(Counter, Node, 0)

 protected:
  Counter() = default;

 public:
  explicit Counter(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    Node::Load(ae::Version<2>{}, dnv);
    dnv(value);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    Node::Save(ae::Version<2>{}, dnv);
    dnv(value);
  }

  std::int32_t value{0};

  void Apply(BumpEvent const& event);
};

class BumpEvent : public EventFor<Counter, BumpEvent> {
  APPTRAVERSE_OBJECT(BumpEvent, Event, 0)

 protected:
  BumpEvent() = default;

 public:
  explicit BumpEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(delta))

  std::int32_t delta{0};
};

class RuntimeRoot : public ae::Obj {
  APPTRAVERSE_OBJECT(RuntimeRoot, ae::Obj, 0)

 protected:
  RuntimeRoot() = default;

 public:
  explicit RuntimeRoot(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(counter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(counter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(counter);
  }

  Counter::ptr counter;
};

APPTRAVERSE_REGISTER(Counter);
APPTRAVERSE_REGISTER(BumpEvent);
APPTRAVERSE_REGISTER(RuntimeRoot);

void Counter::Apply(BumpEvent const& event) {
  value += event.delta;
  NoteMaterializedChange();
}

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

constexpr auto kStepTimeout = std::chrono::seconds{30};

// Minimal runtime host: one presentation root with one Node, and a UiMirror
// that is constructed but never publishes, so these tests observe only the
// work queue and the pending bookkeeping.
struct Host {
  ae::RamDomainStorage model_storage;
  ae::RamDomainStorage ui_storage;
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};
  RuntimeRoot::ptr root;
  UiMirror mirror;
  ModelRuntime runtime;

  Host()
      : root{RuntimeRoot::ptr::Create(ae::CreateWith{model_domain})},
        mirror{ui_domain, ui_storage,
               [](std::uint32_t, PublicationChannel<3>*) {}},
        runtime{*root, mirror} {
    runtime.AddPresentationRoot(*root);
  }

  Counter& AttachCounter() {
    root->counter = Counter::ptr::Create(ae::CreateWith{model_domain});
    InitializeRuntimeNode(*root->counter);
    runtime.AttachNode(*root->counter, *root);
    return *root->counter;
  }

  void Bump(Counter& counter, std::int32_t delta) {
    auto event = BumpEvent::ptr::Create(ae::CreateWith{model_domain});
    event->delta = delta;
    counter.Commit(event);
  }
};

void TestAttachNodeDoesNotBindNodeBase() {
  Host host;
  Counter& counter = host.AttachCounter();

  CHECK(counter.HasMaterializedChangeNotifier());
  CHECK(counter.base.is_valid());
  CHECK(counter.base.is_loaded());
  CHECK(!counter.base->HasMaterializedChangeNotifier());

  host.runtime.DetachNode(counter, *host.root);
  CHECK(!counter.HasMaterializedChangeNotifier());
  CHECK(!counter.base->HasMaterializedChangeNotifier());
}

// DetachNode is the boundary where a Node leaves a presentation root, so it
// must also drop the Node from the deferred publication set.
void TestDetachNodeClearsPendingPublication() {
  Host host;
  Counter& counter = host.AttachCounter();
  auto const root_id = host.root->obj_id.id();

  CHECK(!host.runtime.HasPending(root_id));
  host.Bump(counter, 3);
  CHECK(counter.value == 3);
  CHECK(host.runtime.HasPending(root_id));

  host.runtime.DetachNode(counter, *host.root);
  CHECK(!host.runtime.HasPending(root_id));
  CHECK(!host.runtime.IsInExecutionList(counter));
  CHECK(!host.runtime.IsMappedToPresentationRoot(counter, root_id));
}

// Work posted before RequestStop runs before Join returns; work posted after
// it is neither queued nor executed. The acceptance decision and the queue
// push share one critical section, so no timing window can reorder them.
void TestStopBoundaryAcceptsQueuedWorkAndRejectsLater() {
  Host host;
  std::atomic<int> executed{0};
  std::promise<void> entered;
  auto entered_future = entered.get_future();
  std::promise<void> release;
  auto release_future = release.get_future();

  host.runtime.Start();

  // Occupy the model thread so the next Post is observably still queued.
  host.runtime.Post([&] {
    entered.set_value();
    release_future.wait();
    ++executed;
  });
  CHECK(entered_future.wait_for(kStepTimeout) == std::future_status::ready);

  host.runtime.Post([&] { ++executed; });

  host.runtime.RequestStop();
  host.runtime.Post([&] { ++executed; });

  release.set_value();
  host.runtime.Join();

  CHECK(executed.load() == 2);

  // Still rejected after the thread is gone, and still no deadlock.
  host.runtime.Post([&] { ++executed; });
  host.runtime.RequestStop();
  host.runtime.Join();
  CHECK(executed.load() == 2);
}

// A stopped runtime that never started a thread must not leave work pending
// in a way that blocks destruction.
void TestStopWithoutStartDropsWork() {
  Host host;
  std::atomic<int> executed{0};

  host.runtime.RequestStop();
  host.runtime.Post([&] { ++executed; });
  host.runtime.Join();
  CHECK(executed.load() == 0);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestAttachNodeDoesNotBindNodeBase();
  apptraverse::test::TestDetachNodeClearsPendingPublication();
  apptraverse::test::TestStopBoundaryAcceptsQueuedWorkAndRejectsLater();
  apptraverse::test::TestStopWithoutStartDropsWork();
  std::cout << "model_runtime_stop_test OK\n";
  return 0;
}
