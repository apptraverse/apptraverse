#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/event_for.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/node_for.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"

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

ae::ObjId const kRootId{0xA5A50001u};

class RuntimeBumpEvent;

class RuntimeTestRoot : public NodeFor<RuntimeTestRoot> {
  APPTRAVERSE_OBJECT(RuntimeTestRoot, Node, 0)

 protected:
  RuntimeTestRoot() = default;

 public:
  explicit RuntimeTestRoot(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, value);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, value);
  }

  std::int32_t value{0};

  void Apply(RuntimeBumpEvent const& event);
};

class RuntimeBumpEvent : public EventFor<RuntimeTestRoot, RuntimeBumpEvent> {
  APPTRAVERSE_OBJECT(RuntimeBumpEvent, Event, 0)

 protected:
  RuntimeBumpEvent() = default;

 public:
  explicit RuntimeBumpEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(delta))

  std::int32_t delta{0};
};

APPTRAVERSE_REGISTER(RuntimeTestRoot);
APPTRAVERSE_REGISTER(RuntimeBumpEvent);

void RuntimeTestRoot::Apply(RuntimeBumpEvent const& event) {
  value += event.delta;
  NoteMaterializedChange();
}

struct Sync {
  std::mutex mu;
  std::condition_variable cv;
};

void CommitBump(RuntimeTestRoot& root, ae::Domain& domain, std::int32_t delta) {
  auto ev = RuntimeBumpEvent::ptr::Create(ae::CreateWith{domain});
  ev->delta = delta;
  root.Commit(std::move(ev));
}

std::int32_t ReadUiValue(RuntimeTestRoot& ui_root) { return ui_root.value; }

void ApplyPublicationBytes(std::vector<std::uint8_t> const& bytes, ae::Domain& ui_domain, ae::RamDomainStorage& ui_storage, bool initial, RuntimeTestRoot::ptr& ui_root) {
  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  if (initial) { auto ui_obj = LoadInitialPublication(in, ui_domain, ui_storage); CHECK(ui_obj); CHECK(ui_obj->obj_id == kRootId); ui_root = RuntimeTestRoot::ptr::MakeFromThis(static_cast<RuntimeTestRoot*>(ui_obj.get())); return; }
  ApplyStructuralPublication(in, ui_domain, ui_storage);
}

void TestDirectIncrementalPublicationApply() {
  ae::RamDomainStorage model_storage;
  ae::RamDomainStorage ui_storage;
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};

  auto root = RuntimeTestRoot::ptr::Create(
      ae::CreateWith{model_domain}.with_id(kRootId));
  InitializeRuntimeNode(*root);
  root->value = 0;
  CommitBump(*root, model_domain, 7);
  CHECK(root->value == 7);

  ByteSink initial_out;
  SerializeInitialPublication(*root, initial_out);
  ByteSource initial_in;
  initial_in.data = initial_out.bytes.data();
  initial_in.size = initial_out.bytes.size();
  auto ui_obj = LoadInitialPublication(initial_in, ui_domain, ui_storage);
  CHECK(ui_obj);
  auto ui_root = RuntimeTestRoot::ptr::MakeFromThis(
      static_cast<RuntimeTestRoot*>(ui_obj.get()));
  RuntimeTestRoot* const ui_root_ptr = &*ui_root;
  CHECK(ReadUiValue(*ui_root) == 7);
  CHECK(!ui_root->base.is_valid());
  CHECK(ui_root->journal.empty());

  CommitBump(*root, model_domain, 5);
  CHECK(root->value == 12);
  CHECK(ReadUiValue(*ui_root) == 7);

  std::int32_t const model_value_before_publish = root->value;
  std::size_t const model_journal_size = root->journal.size();
  bool const model_had_base = root->base.is_valid();
  std::uint64_t const published_generation = root->Generation();

  std::uint64_t const processed_sequence = 42;
  ByteSink out;
  out.write(&processed_sequence, sizeof(processed_sequence));
  std::size_t const prefix_size = out.bytes.size();
  SerializeIncrementalNodePublication(*root, out);
  CHECK(out.bytes.size() > prefix_size);
  CHECK(out.bytes[0] == static_cast<std::uint8_t>(processed_sequence & 0xFFu));

  ByteSource in;
  in.data = out.bytes.data();
  in.size = out.bytes.size();
  in.pos = 0;
  std::uint64_t read_sequence = 0;
  in.read(&read_sequence, sizeof(read_sequence));
  CHECK(in.ok);
  CHECK(read_sequence == processed_sequence);

  ae::Obj& applied =
      ApplyIncrementalPublication(in, ui_domain, ui_storage);
  CHECK(&applied == ui_root_ptr);
  CHECK(ReadUiValue(*ui_root) == 12);
  CHECK(ui_root->Generation() == published_generation);
  CHECK(!ui_root->base.is_valid());
  CHECK(ui_root->journal.empty());
  CHECK(in.pos == in.size);
  CHECK(root->value == model_value_before_publish);
  CHECK(root->journal.size() == model_journal_size);
  CHECK(root->base.is_valid() == model_had_base);
}

void TestInitialAndIncrementalPublication() {
  ae::RamDomainStorage model_storage;
  ae::RamDomainStorage ui_storage;
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};

  auto root = RuntimeTestRoot::ptr::Create(
      ae::CreateWith{model_domain}.with_id(kRootId));
  InitializeRuntimeNode(*root);
  CommitBump(*root, model_domain, 7);

  ApplicationModelRuntime runtime;
  std::promise<void> initial_published;
  std::promise<void> incremental_published;
  std::future<void> initial_future = initial_published.get_future();
  std::future<void> incremental_future = incremental_published.get_future();
  RuntimeTestRoot::ptr ui_root;

  std::thread model_thread{[&] {
    runtime.Run(
        model_domain, *root,
        [&](ApplicationPublicationKind kind) {
          if (kind == ApplicationPublicationKind::Initial) {
            initial_published.set_value();
          } else {
            incremental_published.set_value();
          }
        });
  }};

  initial_future.wait();
  std::vector<std::uint8_t> initial_bytes = runtime.TakePublishedCopy();
  runtime.NotifyPublicationConsumed();
  ApplyPublicationBytes(initial_bytes, ui_domain, ui_storage, true, ui_root);
  CHECK(ReadUiValue(*ui_root) == 7);

  runtime.Post([&](ae::Domain& domain) { CommitBump(*root, domain, 5); });

  incremental_future.wait();
  CHECK(ReadUiValue(*ui_root) == 7);
  std::vector<std::uint8_t> incremental_bytes = runtime.TakePublishedCopy();
  runtime.NotifyPublicationConsumed();
  ApplyPublicationBytes(incremental_bytes, ui_domain, ui_storage, false, ui_root);
  CHECK(ReadUiValue(*ui_root) == 12);

  runtime.RequestStop();
  model_thread.join();
}

void TestCoalesceAndBackpressure() {
  ae::RamDomainStorage model_storage;
  ae::RamDomainStorage ui_storage;
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};

  auto root = RuntimeTestRoot::ptr::Create(
      ae::CreateWith{model_domain}.with_id(kRootId));
  InitializeRuntimeNode(*root);

  ApplicationModelRuntime runtime;
  Sync sync;
  RuntimeTestRoot::ptr ui_root;
  bool initial_ready = false;
  bool incremental_ready = false;
  bool second_incremental_ready = false;
  std::vector<std::uint8_t> initial_bytes;
  std::vector<std::uint8_t> incremental_bytes;
  std::vector<std::uint8_t> second_incremental_bytes;
  std::atomic<bool> backpressure_work_done{false};

  std::thread model_thread{[&] {
    runtime.Run(
        model_domain, *root,
        [&](ApplicationPublicationKind kind) {
          if (kind == ApplicationPublicationKind::Initial) { initial_ready = true; } else if (!incremental_ready) { incremental_ready = true; } else { second_incremental_ready = true; } sync.cv.notify_all();
        });
  }};

  {
    std::unique_lock<std::mutex> lock{sync.mu};
    sync.cv.wait(lock, [&] { return initial_ready; });
  }
  initial_bytes = runtime.TakePublishedCopy();
  runtime.NotifyPublicationConsumed();
  ApplyPublicationBytes(initial_bytes, ui_domain, ui_storage, true, ui_root);

  runtime.Post([&](ae::Domain& domain) {
    CommitBump(*root, domain, 1);
    CommitBump(*root, domain, 2);
  });

  {
    std::unique_lock<std::mutex> lock{sync.mu};
    sync.cv.wait(lock, [&] { return incremental_ready; });
  }
  CHECK(runtime.publish_count() == 2);
  CHECK(ReadUiValue(*ui_root) == 0);

  runtime.Post([&](ae::Domain& domain) {
    CommitBump(*root, domain, 100);
    backpressure_work_done.store(true);
    sync.cv.notify_all();
  });

  {
    std::unique_lock<std::mutex> lock{sync.mu};
    sync.cv.wait(lock, [&] { return backpressure_work_done.load(); });
  }
  CHECK(runtime.publish_count() == 2);

  incremental_bytes = runtime.TakePublishedCopy();
  runtime.NotifyPublicationConsumed();
  ApplyPublicationBytes(incremental_bytes, ui_domain, ui_storage, false, ui_root);
  CHECK(ReadUiValue(*ui_root) == 3);

  {
    std::unique_lock<std::mutex> lock{sync.mu};
    sync.cv.wait_for(lock, std::chrono::seconds{5},
                     [&] { return second_incremental_ready; });
  }
  CHECK(second_incremental_ready);
  second_incremental_bytes = runtime.TakePublishedCopy();
  runtime.NotifyPublicationConsumed();
  ApplyPublicationBytes(second_incremental_bytes, ui_domain, ui_storage, false, ui_root);
  CHECK(ReadUiValue(*ui_root) == 103);

  runtime.RequestStop();
  model_thread.join();
}

void TestCallbackCanPostAfterPublication() {
  ae::RamDomainStorage model_storage;
  ae::Domain model_domain{model_storage};
  auto root = RuntimeTestRoot::ptr::Create(
      ae::CreateWith{model_domain}.with_id(kRootId));
  InitializeRuntimeNode(*root);

  ApplicationModelRuntime runtime;
  Sync sync;
  bool initial_ready = false;
  std::atomic<bool> chained_ran{false};

  std::thread model_thread{[&] {
    runtime.Run(model_domain, *root, [&](ApplicationPublicationKind kind) {
      if (kind == ApplicationPublicationKind::Initial) {
        runtime.Post([&](ae::Domain&) { chained_ran = true; });

        runtime.TakePublishedCopy();
        runtime.NotifyPublicationConsumed();
        initial_ready = true;
        sync.cv.notify_all();
        runtime.RequestStop();
      }
    });
  }};

  {
    std::unique_lock<std::mutex> lock{sync.mu};
    sync.cv.wait(lock, [&] { return initial_ready; });
  }
  model_thread.join();
  CHECK(chained_ran.load());
}

void TestStopDrainsAcceptedWork() {
  ae::RamDomainStorage model_storage;
  ae::Domain model_domain{model_storage};
  auto root = RuntimeTestRoot::ptr::Create(
      ae::CreateWith{model_domain}.with_id(kRootId));
  InitializeRuntimeNode(*root);

  ApplicationModelRuntime runtime;
  Sync sync;
  bool initial_ready = false;
  std::atomic<int> executed{0};

  std::thread model_thread{[&] {
    runtime.Run(model_domain, *root, [&](ApplicationPublicationKind kind) {
      if (kind == ApplicationPublicationKind::Initial) {

        runtime.TakePublishedCopy();
        runtime.NotifyPublicationConsumed();
        initial_ready = true;
        sync.cv.notify_all();
      }
    });
  }};

  {
    std::unique_lock<std::mutex> lock{sync.mu};
    sync.cv.wait(lock, [&] { return initial_ready; });
  }

  runtime.Post([&](ae::Domain&) { ++executed; });
  runtime.Post([&](ae::Domain&) { ++executed; });
  runtime.RequestStop();
  runtime.Post([&](ae::Domain&) { ++executed; });

  model_thread.join();
  CHECK(executed.load() == 2);
}

void TestBeforeIncrementalSidecarAtTake() {
  ae::RamDomainStorage model_storage;
  ae::Domain model_domain{model_storage};
  auto root = RuntimeTestRoot::ptr::Create(
      ae::CreateWith{model_domain}.with_id(kRootId));
  InitializeRuntimeNode(*root);

  ApplicationModelRuntime runtime;
  Sync sync;
  bool initial_ready = false;
  bool incremental_ready = false;
  int pending_tag = 0;
  int consumed_tag = -1;

  std::thread model_thread{[&] {
    runtime.Run(
        model_domain, *root,
        [&](ApplicationPublicationKind kind) {
          if (kind == ApplicationPublicationKind::Initial) {
            runtime.TakePublishedCopy();
            runtime.NotifyPublicationConsumed();

            initial_ready = true;
            sync.cv.notify_all();
            return;
          }
          runtime.TakePublishedCopy([&] {
            consumed_tag = pending_tag;
            pending_tag = 0;
          });
          runtime.NotifyPublicationConsumed();

          incremental_ready = true;
          sync.cv.notify_all();
          runtime.RequestStop();
        },
        [&] { pending_tag = 99; });
  }};

  {
    std::unique_lock<std::mutex> lock{sync.mu};
    sync.cv.wait(lock, [&] { return initial_ready; });
  }

  runtime.Post([&](ae::Domain& domain) { CommitBump(*root, domain, 1); });

  {
    std::unique_lock<std::mutex> lock{sync.mu};
    sync.cv.wait(lock, [&] { return incremental_ready; });
  }
  CHECK(consumed_tag == 99);
  CHECK(pending_tag == 0);

  model_thread.join();
}

void TestNotifierDetachedAfterRun() {
  ae::RamDomainStorage model_storage;
  ae::Domain model_domain{model_storage};
  auto root = RuntimeTestRoot::ptr::Create(
      ae::CreateWith{model_domain}.with_id(kRootId));
  InitializeRuntimeNode(*root);

  ApplicationModelRuntime runtime;
  Sync sync;
  bool initial_ready = false;

  std::thread model_thread{[&] {
    runtime.Run(model_domain, *root, [&](ApplicationPublicationKind kind) {
      if (kind == ApplicationPublicationKind::Initial) {
        runtime.TakePublishedCopy();
        runtime.NotifyPublicationConsumed();

        initial_ready = true;
        sync.cv.notify_all();
      }
    });
  }};

  {
    std::unique_lock<std::mutex> lock{sync.mu};
    sync.cv.wait(lock, [&] { return initial_ready; });
  }
  runtime.RequestStop();
  model_thread.join();

  CommitBump(*root, model_domain, 1);
  CHECK(root->value == 1);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestDirectIncrementalPublicationApply();
  apptraverse::test::TestInitialAndIncrementalPublication();
  apptraverse::test::TestCoalesceAndBackpressure();
  apptraverse::test::TestStopDrainsAcceptedWork();
  apptraverse::test::TestCallbackCanPostAfterPublication();
  apptraverse::test::TestBeforeIncrementalSidecarAtTake();
  apptraverse::test::TestNotifierDetachedAfterRun();
  std::cout << "application_model_runtime_test OK\n";
  return 0;
}
