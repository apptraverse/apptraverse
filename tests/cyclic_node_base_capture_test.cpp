#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/event_identity.h"
#include "apptraverse/node_for.h"

namespace apptraverse::test {

class CyclicNode;
class RenameCyclicNodeEvent;

class CyclicPresenter : public ae::Obj {
  AE_OBJECT(CyclicPresenter, Obj, 0)

 protected:
  CyclicPresenter() = default;

 public:
  explicit CyclicPresenter(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(caption), AE_MMBR(node))

  std::string caption;
  ae::ObjPtr<CyclicNode> node;
};

static_assert(!std::is_base_of_v<apptraverse::Node, CyclicPresenter>);

class CyclicNode : public apptraverse::NodeFor<CyclicNode> {
  AE_OBJECT(CyclicNode, Node, 0)

 protected:
  CyclicNode() = default;

 public:
  explicit CyclicNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name), AE_MMBR(presenter))

  std::string name;
  CyclicPresenter::ptr presenter;

  void Apply(RenameCyclicNodeEvent const& event);

  void CaptureBaseStateForTest() { CaptureBaseState(); }

  void RebuildFromBaseAndReplayForTest() { RebuildFromBaseAndReplay(); }

  void CommitEventForTest(apptraverse::Event::ptr event, ae::TimePoint time,
                          ae::ObjId origin) {
    CommitEvent(std::move(event), time, origin);
  }
};

class RenameCyclicNodeEvent
    : public apptraverse::EventFor<CyclicNode, RenameCyclicNodeEvent> {
  AE_OBJECT(RenameCyclicNodeEvent, Event, 0)

 protected:
  RenameCyclicNodeEvent() = default;

 public:
  explicit RenameCyclicNodeEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name))

  std::string name;
};

void CyclicNode::Apply(RenameCyclicNodeEvent const& event) {
  name = event.name;
}

}  // namespace apptraverse::test

namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " (" << __FILE__ << ':'         \
                << __LINE__ << ")\n";                                        \
      return EXIT_FAILURE;                                                   \
    }                                                                        \
  } while (0)

bool ContainsObj(ae::RamDomainStorage const& storage, ae::ObjId::Type id) {
  return storage.state.find(ae::ObjId{id}) != storage.state.end();
}

}  // namespace

int main() {
  using apptraverse::EventIdentity;
  using apptraverse::EventRecordOrigin;
  using apptraverse::test::CyclicNode;
  using apptraverse::test::CyclicPresenter;
  using apptraverse::test::RenameCyclicNodeEvent;

  ae::ObjId const local_origin{9001};

  ae::RamDomainStorage storage;
  ae::Domain domain1{ae::Now(), storage};
  CHECK(storage.state.empty());

  CyclicPresenter::ptr presenter =
      CyclicPresenter::ptr::Create(ae::CreateWith{domain1}.with_id(500));
  CHECK(static_cast<bool>(presenter));
  presenter->caption = "Alice presenter";

  CyclicNode::ptr base =
      CyclicNode::ptr::Create(ae::CreateWith{domain1}.with_id(1000));
  CHECK(static_cast<bool>(base));
  base->name = "Uninitialized base";
  CHECK(!base->base.is_valid());
  CHECK(base->journal.empty());
  CHECK(!base->presenter.is_valid());

  CyclicNode::ptr live =
      CyclicNode::ptr::Create(ae::CreateWith{domain1}.with_id(100));
  CHECK(static_cast<bool>(live));
  live->name = "Alice";
  live->base = base;
  live->presenter = presenter;
  presenter->node = live;

  CHECK(live->presenter.is_loaded());
  CHECK(presenter->node.is_loaded());
  CHECK(presenter->node.Load().get() == live.Load().get());
  CHECK(storage.state.empty());
  CHECK(live->journal.empty());

  auto* live_address = live.Load().get();
  auto* base_address = base.Load().get();
  auto* presenter_address = presenter.Load().get();
  CHECK(live_address != nullptr);
  CHECK(base_address != nullptr);
  CHECK(presenter_address != nullptr);

  live->CaptureBaseStateForTest();

  CHECK(storage.state.size() == 2);
  CHECK(ContainsObj(storage, 1000));
  CHECK(ContainsObj(storage, 500));
  CHECK(!ContainsObj(storage, 100));
  CHECK(!ContainsObj(storage, 200));

  CHECK(live.id().id() == 100);
  CHECK(live.Load().get() == live_address);
  CHECK(live->name == "Alice");
  CHECK(live->base.id().id() == 1000);
  CHECK(live->base.is_loaded());
  CHECK(live->presenter.id().id() == 500);
  CHECK(live->presenter.is_loaded());
  CHECK(live->presenter.Load().get() == presenter_address);
  CHECK(live->journal.empty());

  auto* captured_base = live->base.Load().as<CyclicNode>();
  CHECK(captured_base != nullptr);
  CHECK(captured_base == base_address);
  CHECK(captured_base->name == "Alice");
  CHECK(!captured_base->base.is_valid());
  CHECK(captured_base->journal.empty());
  CHECK(captured_base->presenter.is_valid());
  CHECK(captured_base->presenter.is_loaded());
  CHECK(captured_base->presenter.id().id() == 500);
  CHECK(captured_base->presenter.Load().get() == live->presenter.Load().get());

  CHECK(presenter->caption == "Alice presenter");
  CHECK(presenter->node.is_valid());
  CHECK(presenter->node.is_loaded());
  CHECK(presenter->node.id().id() == 100);
  CHECK(presenter->node.Load().get() == live_address);
  CHECK(presenter->node.Load().get() != base_address);
  CHECK(captured_base->presenter->node.Load().get() == live_address);

  RenameCyclicNodeEvent::ptr rename_event = RenameCyclicNodeEvent::ptr::Create(
      ae::CreateWith{domain1}.with_id(200));
  CHECK(static_cast<bool>(rename_event));
  rename_event->name = "Alice Cooper";
  ae::TimePoint const rename_time{std::chrono::microseconds{100}};
  live->CommitEventForTest(rename_event, rename_time, local_origin);

  CHECK(live->name == "Alice Cooper");
  CHECK(live->journal.size() == 1);
  CHECK((live->journal[0].identity == EventIdentity{local_origin, 1}));
  CHECK(live->journal[0].origin == EventRecordOrigin::kLocal);
  CHECK(live->journal[0].recipients.empty());
  CHECK(captured_base->name == "Alice");
  CHECK(presenter.Load().get() == presenter_address);
  CHECK(presenter->node.Load().get() == live_address);

  live->name = "Transient value";
  live->RebuildFromBaseAndReplayForTest();

  CHECK(live->name == "Alice Cooper");
  CHECK(live.id().id() == 100);
  CHECK(live->base.id().id() == 1000);
  CHECK(live->journal.size() == 1);
  CHECK(live->presenter.Load().get() == presenter_address);
  CHECK(presenter->node.Load().get() == live_address);
  CHECK(live->base.Load().as<CyclicNode>()->presenter.Load().get() ==
        live->presenter.Load().get());
  CHECK(live->base.Load().as<CyclicNode>()->name == "Alice");

  live.Save();

  CHECK(storage.state.size() == 4);
  CHECK(ContainsObj(storage, 100));
  CHECK(ContainsObj(storage, 1000));
  CHECK(ContainsObj(storage, 500));
  CHECK(ContainsObj(storage, 200));

  ae::Domain domain2{ae::Now(), storage};
  CyclicNode::ptr loaded =
      CyclicNode::ptr::Declare(ae::CreateWith{domain2}.with_id(100));
  loaded.Load();

  CHECK(loaded.is_valid());
  CHECK(loaded.is_loaded());
  CHECK(loaded.id().id() == 100);
  CHECK(loaded->name == "Alice Cooper");
  CHECK(loaded->base.id().id() == 1000);
  CHECK(loaded->journal.size() == 1);
  CHECK(loaded->journal[0].event.id().id() == 200);
  CHECK(loaded->journal[0].origin == EventRecordOrigin::kLocal);
  CHECK(loaded->journal[0].recipients.empty());
  auto* loaded_event =
      loaded->journal[0].event.Load().as<RenameCyclicNodeEvent>();
  CHECK(loaded_event != nullptr);
  CHECK(loaded_event->name == "Alice Cooper");

  auto* loaded_base = loaded->base.Load().as<CyclicNode>();
  auto* loaded_presenter = loaded->presenter.Load().as<CyclicPresenter>();
  CHECK(loaded_base != nullptr);
  CHECK(loaded_presenter != nullptr);
  CHECK(loaded_base->name == "Alice");
  CHECK(!loaded_base->base.is_valid());
  CHECK(loaded_base->journal.empty());
  CHECK(loaded->presenter.id().id() == 500);
  CHECK(loaded_base->presenter.id().id() == 500);
  CHECK(loaded->presenter.Load().get() == loaded_base->presenter.Load().get());
  CHECK(loaded_presenter->caption == "Alice presenter");
  CHECK(loaded_presenter->node.id().id() == 100);
  CHECK(loaded_presenter->node.is_loaded());
  CHECK(loaded_presenter->node.Load().get() == loaded.Load().get());
  CHECK(loaded_presenter->node.Load().get() != loaded_base);

  auto* presenter_after_reload = loaded->presenter.Load().get();
  loaded->name = "Transient value";
  loaded->RebuildFromBaseAndReplayForTest();

  CHECK(loaded->name == "Alice Cooper");
  CHECK(loaded->presenter.Load().get() == presenter_after_reload);
  CHECK(loaded_presenter->node.Load().get() == loaded.Load().get());
  CHECK(loaded->base.Load().as<CyclicNode>()->presenter.Load().get() ==
        loaded->presenter.Load().get());
  CHECK(loaded->journal.size() == 1);
  CHECK(loaded->journal[0].event.id().id() == 200);

  return EXIT_SUCCESS;
}
