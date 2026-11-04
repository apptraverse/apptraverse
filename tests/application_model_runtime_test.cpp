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
#include "apptraverse/application_model_session.h"
#include "apptraverse/distill.h"
#include "apptraverse/event_for.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/node_for.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/graph_walk.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui_mirror.h"

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
class RuntimeChildBumpEvent;
class RuntimeTestChild;

class RuntimeTestChild : public NodeFor<RuntimeTestChild> {
  APPTRAVERSE_OBJECT(RuntimeTestChild, Node, 0)

 protected:
  RuntimeTestChild() = default;

 public:
  explicit RuntimeTestChild(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(marker))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, marker);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, marker);
  }

  std::int32_t marker{0};

  void Apply(RuntimeChildBumpEvent const& event);
};

class RuntimeTestRoot : public NodeFor<RuntimeTestRoot> {
  APPTRAVERSE_OBJECT(RuntimeTestRoot, Node, 0)

 protected:
  RuntimeTestRoot() = default;

 public:
  explicit RuntimeTestRoot(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value), AE_MMBR(child), AE_MMBR(lazy_child))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, value, child, lazy_child);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, value, child, lazy_child);
  }

  std::int32_t value{0};
  RuntimeTestChild::ptr child;
  RuntimeTestChild::ptr lazy_child;

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

class RuntimeChildBumpEvent
    : public EventFor<RuntimeTestChild, RuntimeChildBumpEvent> {
  APPTRAVERSE_OBJECT(RuntimeChildBumpEvent, Event, 0)

 protected:
  RuntimeChildBumpEvent() = default;

 public:
  explicit RuntimeChildBumpEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(delta))

  std::int32_t delta{0};
};

APPTRAVERSE_REGISTER(RuntimeTestChild);
APPTRAVERSE_REGISTER(RuntimeTestRoot);
APPTRAVERSE_REGISTER(RuntimeBumpEvent);
APPTRAVERSE_REGISTER(RuntimeChildBumpEvent);

void RuntimeTestChild::Apply(RuntimeChildBumpEvent const& event) {
  marker += event.delta;
  NoteMaterializedChange();
}

void RuntimeTestRoot::Apply(RuntimeBumpEvent const& event) {
  value += event.delta;
  NoteMaterializedChange();
}

ae::ObjId const kChildId{0xA5A50002u};
ae::ObjId const kLazyChildId{0xA5A50003u};
ae::ObjId const kSessionChildId{0xA5A50005u};
ae::ObjId const kGraphWalkRootId{0xA5A50010u};
ae::ObjId const kGraphWalkListId{0xA5A50011u};
ae::ObjId const kGraphWalkRowId{0xA5A50012u};
ae::ObjId const kGraphWalkLazyRowId{0xA5A50013u};

class GraphWalkRow : public NodeFor<GraphWalkRow> {
  APPTRAVERSE_OBJECT(GraphWalkRow, Node, 0)

 protected:
  GraphWalkRow() = default;

 public:
  explicit GraphWalkRow(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(payload))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, payload);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, payload);
  }

  std::int32_t payload{0};
};

class GraphWalkListBase : public NodeFor<GraphWalkListBase> {
  APPTRAVERSE_OBJECT(GraphWalkListBase, Node, 0)

 protected:
  GraphWalkListBase() = default;

 public:
  explicit GraphWalkListBase(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(rows))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, rows);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, rows);
  }

  std::vector<GraphWalkRow::ptr> rows;
};

class GraphWalkDerivedList
    : public NodeFor<GraphWalkDerivedList, GraphWalkListBase> {
  APPTRAVERSE_OBJECT(GraphWalkDerivedList, GraphWalkListBase, 0)

 protected:
  GraphWalkDerivedList() = default;

 public:
  explicit GraphWalkDerivedList(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(list_label), AE_MMBR(pinned_row))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, list_label, pinned_row);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, list_label, pinned_row);
  }

  std::int32_t list_label{0};
  GraphWalkRow::ptr pinned_row;
};

class GraphWalkRoot : public NodeFor<GraphWalkRoot> {
  APPTRAVERSE_OBJECT(GraphWalkRoot, Node, 0)

 protected:
  GraphWalkRoot() = default;

 public:
  explicit GraphWalkRoot(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(list), AE_MMBR(lazy_row))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, list, lazy_row);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, list, lazy_row);
  }

  GraphWalkDerivedList::ptr list;
  GraphWalkRow::ptr lazy_row;
};

APPTRAVERSE_REGISTER(GraphWalkRow);
APPTRAVERSE_REGISTER(GraphWalkListBase);
APPTRAVERSE_REGISTER(GraphWalkDerivedList);
APPTRAVERSE_REGISTER(GraphWalkRoot);

std::size_t CountLiveEdgesTo(ae::Obj& obj, ae::ObjId target) {
  std::size_t count = 0;
  ForEachGraphEdgeObjPtrOnObject(obj, [&](ae::ObjectPtrBase& edge) {
    if (edge.id() == target) {
      ++count;
    }
  });
  return count;
}

std::size_t CountLiveEdges(ae::Obj& obj) {
  std::size_t count = 0;
  ForEachGraphEdgeObjPtrOnObject(obj, [&](ae::ObjectPtrBase&) { ++count; });
  return count;
}

bool LiveNodesContainId(std::vector<Node*> const& live, ae::ObjId id) {
  for (Node* node : live) {
    if (node->obj_id == id) {
      return true;
    }
  }
  return false;
}

struct Sync {
  std::mutex mu;
  std::condition_variable cv;
};

void RunModelUntilInitialReady(ApplicationModelRuntime& runtime,
                               ae::Domain& model_domain, Node& root,
                               Sync& sync, bool& initial_ready) {
  std::thread model_thread{[&] {
    runtime.Run(model_domain, root, [&](ApplicationPublicationKind kind) {
      if (kind == ApplicationPublicationKind::Initial) {
        runtime.TakePublishedCopy();
        runtime.NotifyPublicationConsumed();
        {
          std::lock_guard<std::mutex> lock{sync.mu};
          initial_ready = true;
        }
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
}

void BumpChildMarker(RuntimeTestChild& child, ae::Domain& domain,
                     std::int32_t delta) {
  auto ev = RuntimeChildBumpEvent::ptr::Create(ae::CreateWith{domain});
  ev->delta = delta;
  child.Commit(std::move(ev));
}

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

void TestUiMirrorBatchPublication() {
  ae::RamDomainStorage model_storage;
  ae::RamDomainStorage ui_storage;
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};

  auto root = RuntimeTestRoot::ptr::Create(
      ae::CreateWith{model_domain}.with_id(kRootId));
  InitializeRuntimeNode(*root);
  CommitBump(*root, model_domain, 7);

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

  UiMirror mirror(ui_domain, ui_storage, nullptr);
  std::uint32_t const root_wire_id = kRootId.id();

  CommitBump(*root, model_domain, 5);
  CHECK(root->value == 12);
  CHECK(ReadUiValue(*ui_root) == 7);

  std::int32_t const model_value_before_publish = root->value;
  std::size_t const model_journal_size = root->journal.size();
  std::uint64_t const published_generation = root->Generation();

  std::vector<Node*> changed{&*root};
  CHECK(mirror.Publish(root_wire_id, changed));
  CHECK(ReadUiValue(*ui_root) == 7);
  CHECK(!mirror.Publish(root_wire_id, changed));

  auto& channel = mirror.ChannelFor(root_wire_id);
  UiApplyResult applied = mirror.ApplyPublished(channel, root_wire_id);
  CHECK(ReadUiValue(*ui_root) == 12);
  CHECK(ui_root_ptr == &*ui_root);
  CHECK(applied.changed_obj_ids.size() == 1);
  CHECK(applied.changed_obj_ids[0] == root_wire_id);
  CHECK(ui_root->Generation() == published_generation);
  CHECK(!ui_root->base.is_valid());
  CHECK(ui_root->journal.empty());
  CHECK(root->value == model_value_before_publish);
  CHECK(root->journal.size() == model_journal_size);
  CHECK(!channel.has_unread_published());
  CHECK(mirror.Publish(root_wire_id, changed));
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
          std::lock_guard<std::mutex> lock{sync.mu};
          if (kind == ApplicationPublicationKind::Initial) {
            initial_ready = true;
          } else if (!incremental_ready) {
            incremental_ready = true;
          } else {
            second_incremental_ready = true;
          }
          sync.cv.notify_all();
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
        {
          std::lock_guard<std::mutex> lock{sync.mu};
          initial_ready = true;
        }
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

void TestModelSessionLoadRunPersist() {
  ae::RamDomainStorage model_storage;
  {
    ae::Domain prep_domain{model_storage};
    auto root = RuntimeTestRoot::ptr::Create(
        ae::CreateWith{prep_domain}.with_id(kRootId));
    root->value = 7;
    InitializeRuntimeNode(*root);
    ae::DomainGraph graph{&prep_domain};
    CHECK(graph.SaveRoot(root.Load(), kRootId).IsOk());
  }

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  RuntimeTestRoot::ptr ui_root;

  ApplicationModelRuntime runtime;
  Sync sync;
  int prepare_calls = 0;
  int persist_calls = 0;
  int before_incremental_calls = 0;
  ae::ObjId base_id_at_prepare{};
  std::size_t journal_size_at_prepare = 0;
  bool initial_ready = false;
  bool incremental_ready = false;
  int session_result = -1;

  ApplicationModelSessionHooks<RuntimeTestRoot> hooks;
  hooks.prepare = [&](RuntimeTestRoot& root) {
    ++prepare_calls;
    CHECK(root.value == 7);
    CHECK(root.base.is_valid());
    base_id_at_prepare = root.base.id();
    journal_size_at_prepare = root.journal.size();
  };
  hooks.before_incremental = [&](RuntimeTestRoot& root) {
    ++before_incremental_calls;
    CHECK(root.value == 12);
  };
  hooks.persist = [&](RuntimeTestRoot& root) -> int {
    ++persist_calls;
    CHECK(root.value == 13);
    RuntimeTestRoot::ptr root_ref = RuntimeTestRoot::ptr::MakeFromThis(&root);
    ae::DomainGraph graph{root.domain};
    CHECK(graph.SaveRoot(root_ref.Load(), kRootId).IsOk());
    return 0;
  };

  std::thread model_thread{[&] {
    session_result = RunApplicationModelSession(
        model_storage, kRootId, runtime,
        [&](ApplicationPublicationKind kind) {
          if (kind == ApplicationPublicationKind::Initial) {
            std::vector<std::uint8_t> bytes = runtime.TakePublishedCopy();
            runtime.NotifyPublicationConsumed();
            ApplyPublicationBytes(bytes, ui_domain, ui_storage, true, ui_root);
            CHECK(ReadUiValue(*ui_root) == 7);
            runtime.Post([&](ae::Domain& domain) {
              CommitBump(
                  *static_cast<RuntimeTestRoot*>(domain.Find(kRootId).get()),
                  domain, 5);
            });
            initial_ready = true;
            sync.cv.notify_all();
            return;
          }
          std::vector<std::uint8_t> bytes = runtime.TakePublishedCopy();
          runtime.NotifyPublicationConsumed();
          ApplyPublicationBytes(bytes, ui_domain, ui_storage, false, ui_root);
          CHECK(ReadUiValue(*ui_root) == 12);
          runtime.Post([&](ae::Domain& domain) {
            CommitBump(
                *static_cast<RuntimeTestRoot*>(domain.Find(kRootId).get()),
                domain, 1);
          });
          runtime.RequestStop();
          incremental_ready = true;
          sync.cv.notify_all();
        },
        hooks);
  }};

  {
    std::unique_lock<std::mutex> lock{sync.mu};
    sync.cv.wait(lock, [&] { return initial_ready; });
  }
  {
    std::unique_lock<std::mutex> lock{sync.mu};
    sync.cv.wait(lock, [&] { return incremental_ready; });
  }
  model_thread.join();

  CHECK(session_result == 0);
  CHECK(prepare_calls == 1);
  CHECK(persist_calls == 1);
  CHECK(before_incremental_calls == 1);

  ae::Domain cold_domain{model_storage};
  auto cold_root = LoadApplication<RuntimeTestRoot>(cold_domain, kRootId);
  CHECK(cold_root->value == 13);
  CHECK(cold_root->base.is_valid());
  CHECK(cold_root->base.id() == base_id_at_prepare);
  CHECK(cold_root->journal.size() >= journal_size_at_prepare);
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
  RunModelUntilInitialReady(runtime, model_domain, *root, sync, initial_ready);

  CHECK(!root->HasMaterializedChangeNotifier());
  CommitBump(*root, model_domain, 1);
  CHECK(root->value == 1);
}

void TestLiveGraphWalkTraversesBaseClassRowsField() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto root = GraphWalkRoot::ptr::Create(
      ae::CreateWith{domain}.with_id(kGraphWalkRootId));
  auto list = GraphWalkDerivedList::ptr::Create(
      ae::CreateWith{domain}.with_id(kGraphWalkListId));
  auto row = GraphWalkRow::ptr::Create(
      ae::CreateWith{domain}.with_id(kGraphWalkRowId));
  auto lazy_row = GraphWalkRow::ptr::Declare(
      ae::CreateWith{domain}
          .with_id(kGraphWalkLazyRowId)
          .with_flags(ae::ObjFlags::kUnloaded));
  InitializeRuntimeNode(*root);
  InitializeRuntimeNode(*list);
  InitializeRuntimeNode(*row);
  list->list_label = 99;
  list->rows.push_back(row);
  root->list = list;
  root->lazy_row = lazy_row;

  // Base-class rows[0] and derived pinned_row are two edges to one row; each
  // field is visited exactly once.
  CHECK(CountLiveEdgesTo(*list, kGraphWalkRowId) == 1);
  list->pinned_row = row;
  CHECK(CountLiveEdgesTo(*list, kGraphWalkRowId) == 2);
  CHECK(CountLiveEdges(*list) == 2);
  CHECK(CountLiveEdgesTo(*root, kGraphWalkListId) == 1);
  CHECK(CountLiveEdgesTo(*root, kGraphWalkLazyRowId) == 1);
  CHECK(CountLiveEdges(*root) == 2);
  CHECK(!lazy_row.is_loaded());

  std::vector<Node*> live;
  CollectLiveReachableNodes(*root, live);
  CHECK(LiveNodesContainId(live, kGraphWalkRootId));
  CHECK(LiveNodesContainId(live, kGraphWalkListId));
  CHECK(LiveNodesContainId(live, kGraphWalkRowId));
  CHECK(!LiveNodesContainId(live, kGraphWalkLazyRowId));
  CHECK(!lazy_row.is_loaded());

  // An unloaded reference keeps its ObjId while another owner keeps the
  // object in the Domain; it is still not a live edge.
  root->list.Reset();
  CHECK(root->list.is_valid());
  CHECK(domain.Find(kGraphWalkListId));
  CollectLiveReachableNodes(*root, live);
  CHECK(LiveNodesContainId(live, kGraphWalkRootId));
  CHECK(!LiveNodesContainId(live, kGraphWalkListId));
  CHECK(!LiveNodesContainId(live, kGraphWalkRowId));
}

void TestRuntimeNotifierLifetimeOnChildLazyAndDetached() {
  ae::RamDomainStorage model_storage;
  ae::Domain model_domain{model_storage};
  auto root = RuntimeTestRoot::ptr::Create(
      ae::CreateWith{model_domain}.with_id(kRootId));
  auto child = RuntimeTestChild::ptr::Create(
      ae::CreateWith{model_domain}.with_id(kChildId));
  auto lazy = RuntimeTestChild::ptr::Declare(
      ae::CreateWith{model_domain}
          .with_id(kLazyChildId)
          .with_flags(ae::ObjFlags::kUnloaded));
  InitializeRuntimeNode(*root);
  InitializeRuntimeNode(*child);
  root->child = child;
  root->lazy_child = lazy;

  std::vector<Node*> live_before;
  CollectLiveReachableNodes(*root, live_before);
  CHECK(LiveNodesContainId(live_before, kRootId));
  CHECK(LiveNodesContainId(live_before, kChildId));
  CHECK(!LiveNodesContainId(live_before, kLazyChildId));
  CHECK(!lazy.is_loaded());

  ApplicationModelRuntime runtime;
  Sync sync;
  bool initial_ready = false;
  RunModelUntilInitialReady(runtime, model_domain, *root, sync, initial_ready);

  CHECK(!root->HasMaterializedChangeNotifier());
  CHECK(!child->HasMaterializedChangeNotifier());
  CHECK(!lazy.is_loaded());
  BumpChildMarker(*child, model_domain, 3);
  CHECK(child->marker == 3);
}

void TestRuntimeNotifierDetachedChildStillInDomain() {
  ae::RamDomainStorage model_storage;
  ae::Domain model_domain{model_storage};
  auto root = RuntimeTestRoot::ptr::Create(
      ae::CreateWith{model_domain}.with_id(kRootId));
  auto child = RuntimeTestChild::ptr::Create(
      ae::CreateWith{model_domain}.with_id(kChildId));
  InitializeRuntimeNode(*root);
  InitializeRuntimeNode(*child);
  root->child = child;

  ApplicationModelRuntime runtime;
  Sync sync;
  bool initial_ready = false;

  std::thread model_thread{[&] {
    runtime.Run(model_domain, *root, [&](ApplicationPublicationKind kind) {
      if (kind == ApplicationPublicationKind::Initial) {
        runtime.TakePublishedCopy();
        runtime.NotifyPublicationConsumed();
        CHECK(child->HasMaterializedChangeNotifier());
        {
          std::lock_guard<std::mutex> lock{sync.mu};
          initial_ready = true;
        }
        sync.cv.notify_all();
      }
    });
  }};

  {
    std::unique_lock<std::mutex> lock{sync.mu};
    sync.cv.wait(lock, [&] { return initial_ready; });
  }
  runtime.Post([&](ae::Domain&) { root->child = RuntimeTestChild::ptr{}; });
  runtime.RequestStop();
  model_thread.join();

  CHECK(!child->HasMaterializedChangeNotifier());
  BumpChildMarker(*child, model_domain, 2);
  CHECK(child->marker == 2);
}

void TestRuntimeNotifierSessionAddedChildLinkedBeforeStop() {
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
        {
          std::lock_guard<std::mutex> lock{sync.mu};
          initial_ready = true;
        }
        sync.cv.notify_all();
      }
    });
  }};

  {
    std::unique_lock<std::mutex> lock{sync.mu};
    sync.cv.wait(lock, [&] { return initial_ready; });
  }
  runtime.Post([&](ae::Domain& domain) {
    auto session_child = RuntimeTestChild::ptr::Create(
        ae::CreateWith{domain}.with_id(kSessionChildId));
    InitializeRuntimeNode(*session_child, *root);
    root->child = session_child;
  });
  runtime.RequestStop();
  model_thread.join();

  ae::Ptr<ae::Obj> const held = model_domain.Find(kSessionChildId);
  CHECK(held);
  auto& session = static_cast<RuntimeTestChild&>(*held);
  CHECK(!session.HasMaterializedChangeNotifier());
  BumpChildMarker(session, model_domain, 4);
  CHECK(session.marker == 4);
}

void TestRuntimeNotifierOrphanHeldAfterDetachFromLiveAndHistory() {
  ae::RamDomainStorage model_storage;
  ae::Domain model_domain{model_storage};
  auto root = RuntimeTestRoot::ptr::Create(
      ae::CreateWith{model_domain}.with_id(kRootId));
  InitializeRuntimeNode(*root);

  ApplicationModelRuntime runtime;
  Sync sync;
  bool initial_ready = false;
  RuntimeTestChild::ptr held_child;

  std::thread model_thread{[&] {
    runtime.Run(model_domain, *root, [&](ApplicationPublicationKind kind) {
      if (kind == ApplicationPublicationKind::Initial) {
        runtime.TakePublishedCopy();
        runtime.NotifyPublicationConsumed();
        {
          std::lock_guard<std::mutex> lock{sync.mu};
          initial_ready = true;
        }
        sync.cv.notify_all();
      }
    });
  }};

  {
    std::unique_lock<std::mutex> lock{sync.mu};
    sync.cv.wait(lock, [&] { return initial_ready; });
  }

  bool session_linked = false;
  bool child_had_notifier = false;
  runtime.Post([&](ae::Domain& domain) {
    held_child = RuntimeTestChild::ptr::Create(
        ae::CreateWith{domain}.with_id(kSessionChildId));
    InitializeRuntimeNode(*held_child, *root);
    root->child = held_child;
    bool const had_notifier = held_child->HasMaterializedChangeNotifier();
    {
      std::lock_guard<std::mutex> lock{sync.mu};
      child_had_notifier = had_notifier;
      session_linked = true;
    }
    sync.cv.notify_all();
  });
  {
    std::unique_lock<std::mutex> lock{sync.mu};
    sync.cv.wait(lock, [&] { return session_linked; });
  }
  CHECK(child_had_notifier);

  bool detached = false;
  bool link_cleared = false;
  bool live_has_child = true;
  bool persistent_has_child = true;
  runtime.Post([&](ae::Domain&) {
    root->child = RuntimeTestChild::ptr{};
    std::vector<Node*> live;
    CollectLiveReachableNodes(*root, live);
    std::vector<Node*> persistent;
    CollectReachableNodes(*root, persistent);
    bool const cleared = !root->child.is_valid();
    bool const in_live = LiveNodesContainId(live, kSessionChildId);
    bool const in_persistent = LiveNodesContainId(persistent, kSessionChildId);
    {
      std::lock_guard<std::mutex> lock{sync.mu};
      link_cleared = cleared;
      live_has_child = in_live;
      persistent_has_child = in_persistent;
      detached = true;
    }
    sync.cv.notify_all();
  });
  {
    std::unique_lock<std::mutex> lock{sync.mu};
    sync.cv.wait(lock, [&] { return detached; });
  }
  CHECK(link_cleared);
  CHECK(!live_has_child);
  CHECK(!persistent_has_child);
  CHECK(held_child.is_loaded());

  runtime.RequestStop();
  model_thread.join();

  CHECK(!held_child->HasMaterializedChangeNotifier());
  BumpChildMarker(*held_child, model_domain, 6);
  CHECK(held_child->marker == 6);
}

void TestListRowRemoveCommandPublishesIncremental() {
  ae::RamDomainStorage model_storage;
  ae::RamDomainStorage ui_storage;
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};

  auto list = ui::UiListContainer::ptr::Create(ae::CreateWith{model_domain});
  list->selection_index = -1;
  InitializeRuntimeNode(*list);
  std::vector<ui::UiListRow::ptr> held;
  for (int i = 0; i < 3; ++i) {
    auto row = ui::UiListRow::ptr::Create(ae::CreateWith{model_domain});
    InitializeRuntimeNode(*row, *list);
    held.push_back(row);
  }
  list->rows = {held[0], held[1], held[2]};
  ae::ObjId const list_id = list->obj_id;
  ae::ObjId const removed_id = held[1]->obj_id;

  ApplicationModelRuntime runtime;
  Sync sync;
  bool initial_ready = false;
  int incremental_count = 0;
  std::vector<std::uint8_t> initial_bytes;
  std::vector<std::uint8_t> incremental_bytes;
  int enqueued = 0;
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) {
    ++enqueued;
    runtime.Post(std::move(work));
  }};

  std::thread model_thread{[&] {
    runtime.Run(model_domain, *list, [&](ApplicationPublicationKind kind) {
      std::vector<std::uint8_t> bytes = runtime.TakePublishedCopy();
      runtime.NotifyPublicationConsumed();
      std::lock_guard<std::mutex> lock{sync.mu};
      if (kind == ApplicationPublicationKind::Initial) {
        initial_bytes = std::move(bytes);
        initial_ready = true;
      } else {
        incremental_bytes = std::move(bytes);
        ++incremental_count;
      }
      sync.cv.notify_all();
    });
  }};

  {
    std::unique_lock<std::mutex> lock{sync.mu};
    sync.cv.wait(lock, [&] { return initial_ready; });
  }
  ByteSource initial_in;
  initial_in.data = initial_bytes.data();
  initial_in.size = initial_bytes.size();
  auto ui_obj = LoadInitialPublication(initial_in, ui_domain, ui_storage);
  CHECK(ui_obj);
  auto ui_list = ui::UiListContainer::ptr::MakeFromThis(
      static_cast<ui::UiListContainer*>(ui_obj.get()));
  CHECK(ui_list->rows.size() == 3);

  proxy.EnqueueModelWork([list_id, removed_id](ae::Domain& domain) {
    auto remove =
        ui::RemoveUiListRowEvent::ptr::Create(ae::CreateWith{domain});
    remove->row_id = removed_id;
    domain.Find(list_id).as<ui::UiListContainer>()->Commit(std::move(remove));
  });
  // The runtime publishes pending changes before it takes the next work item,
  // so this marker observes whether the remove command alone published.
  bool marker_ran = false;
  int incremental_at_marker = -1;
  runtime.Post([&](ae::Domain&) {
    std::lock_guard<std::mutex> lock{sync.mu};
    incremental_at_marker = incremental_count;
    marker_ran = true;
    sync.cv.notify_all();
  });
  {
    std::unique_lock<std::mutex> lock{sync.mu};
    sync.cv.wait(lock, [&] { return marker_ran; });
  }
  CHECK(incremental_at_marker == 1);

  runtime.RequestStop();
  model_thread.join();
  CHECK((std::vector<ae::ObjId>{list->rows[0].id(), list->rows[1].id()} ==
         std::vector<ae::ObjId>{held[0]->obj_id, held[2]->obj_id}));
  CHECK(list->rows.size() == 2);

  CHECK(ui_list->rows.size() == 3);
  ByteSource incremental_in;
  incremental_in.data = incremental_bytes.data();
  incremental_in.size = incremental_bytes.size();
  ApplyStructuralPublication(incremental_in, ui_domain, ui_storage);
  CHECK(ui_list->rows.size() == 2);
  CHECK(ui_list->rows[0].id() == held[0]->obj_id);
  CHECK(ui_list->rows[1].id() == held[2]->obj_id);
  CHECK(ui_list->selection_index == -1);
  CHECK(enqueued == 1);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::EnsureObjectRegistration();
  apptraverse::ui::EnsureUiPresenterRegistration();
  apptraverse::test::TestUiMirrorBatchPublication();
  apptraverse::test::TestDirectIncrementalPublicationApply();
  apptraverse::test::TestInitialAndIncrementalPublication();
  apptraverse::test::TestCoalesceAndBackpressure();
  apptraverse::test::TestStopDrainsAcceptedWork();
  apptraverse::test::TestCallbackCanPostAfterPublication();
  apptraverse::test::TestBeforeIncrementalSidecarAtTake();
  apptraverse::test::TestModelSessionLoadRunPersist();
  apptraverse::test::TestNotifierDetachedAfterRun();
  apptraverse::test::TestRuntimeNotifierLifetimeOnChildLazyAndDetached();
  apptraverse::test::TestRuntimeNotifierDetachedChildStillInDomain();
  apptraverse::test::TestRuntimeNotifierSessionAddedChildLinkedBeforeStop();
  apptraverse::test::TestLiveGraphWalkTraversesBaseClassRowsField();
  apptraverse::test::TestRuntimeNotifierOrphanHeldAfterDetachFromLiveAndHistory();
  apptraverse::test::TestListRowRemoveCommandPublishesIncremental();
  std::cout << "application_model_runtime_test OK\n";
  return 0;
}
