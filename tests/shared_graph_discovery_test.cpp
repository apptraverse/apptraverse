#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/node_for.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/shared_graph.h"

#include "model/chat.h"
#include "model/chat_events.h"
#include "model/chat_presenter.h"
#include "model/client.h"
#include "model/registration.h"
#include "model/window.h"
#include "model/window_presenter.h"

#include "../examples/single_client_chat/common/graph_builder.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

class FakeChatPresenter : public ChatPresenter {
  APPTRAVERSE_OBJECT(FakeChatPresenter, ChatPresenter, 0)

 protected:
  FakeChatPresenter() = default;

 public:
  explicit FakeChatPresenter(ae::ObjProp prop) : ChatPresenter{prop} {}

  AE_OBJECT_REFLECT()
};

class FakeWindow : public NodeFor<FakeWindow, Window> {
  APPTRAVERSE_OBJECT(FakeWindow, Window, 0)

 protected:
  FakeWindow() = default;

 public:
  explicit FakeWindow(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT()

  void Apply(WindowChangedEvent const&) override {}
};

class FakeWindowPresenter : public WindowPresenter {
  APPTRAVERSE_OBJECT(FakeWindowPresenter, WindowPresenter, 0)

 protected:
  FakeWindowPresenter() = default;

 public:
  explicit FakeWindowPresenter(ae::ObjProp prop) : WindowPresenter{prop} {}

  AE_OBJECT_REFLECT()
};

// --- Generic core discovery graph (test-only types) ---

class TestLeafNode;
class TestLocalNode;
class TestRootNode;

class TestOwnedObject : public ae::Obj {
  APPTRAVERSE_OBJECT(TestOwnedObject, ae::Obj, 0)

 protected:
  TestOwnedObject() = default;

 public:
  explicit TestOwnedObject(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(leaf_a))

  SharedPtr<TestLeafNode> leaf_a;
};

class TestLeafNode : public NodeFor<TestLeafNode> {
  APPTRAVERSE_OBJECT(TestLeafNode, Node, 0)

 protected:
  TestLeafNode() = default;

 public:
  explicit TestLeafNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label), AE_MMBR(next))

  std::string label;
  SharedPtr<Node> next;
};

class TestLocalNode : public NodeFor<TestLocalNode> {
  APPTRAVERSE_OBJECT(TestLocalNode, Node, 0)

 protected:
  TestLocalNode() = default;

 public:
  explicit TestLocalNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(hidden))

  SharedPtr<TestLeafNode> hidden;
};

class TestRootNode : public NodeFor<TestRootNode> {
  APPTRAVERSE_OBJECT(TestRootNode, Node, 0)

 protected:
  TestRootNode() = default;

 public:
  explicit TestRootNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(owned), AE_MMBR(leaf_b), AE_MMBR(local_obj))

  ae::ObjPtr<TestOwnedObject> owned;
  SharedPtr<TestLeafNode> leaf_b;
  LocalPtr<TestLocalNode> local_obj;
};

APPTRAVERSE_REGISTER(FakeChatPresenter);
APPTRAVERSE_REGISTER(FakeWindow);
APPTRAVERSE_REGISTER(FakeWindowPresenter);
APPTRAVERSE_REGISTER(TestOwnedObject);
APPTRAVERSE_REGISTER(TestLeafNode);
APPTRAVERSE_REGISTER(TestLocalNode);
APPTRAVERSE_REGISTER(TestRootNode);

bool ContainsId(std::vector<Node::ptr> const& nodes, ae::ObjId id) {
  for (auto const& node : nodes) {
    if (node.id() == id) {
      return true;
    }
  }
  return false;
}

void ExpectExactIds(std::vector<Node::ptr> const& actual,
                    std::vector<ae::ObjId> expected) {
  std::sort(expected.begin(), expected.end(),
            [](ae::ObjId const& a, ae::ObjId const& b) {
              return a.id() < b.id();
            });
  CHECK(actual.size() == expected.size());
  for (std::size_t i = 0; i < actual.size(); ++i) {
    CHECK(actual[i].id() == expected[i]);
  }
  for (std::size_t i = 1; i < actual.size(); ++i) {
    CHECK(actual[i - 1].id().id() < actual[i].id().id());
  }
}

void TestGenericSharedGraphDiscovery() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};

  auto root = TestRootNode::ptr::Create(ae::CreateWith{domain}.with_id(1));
  auto leaf_a = TestLeafNode::ptr::Create(ae::CreateWith{domain}.with_id(2));
  auto leaf_b = TestLeafNode::ptr::Create(ae::CreateWith{domain}.with_id(3));
  auto local_obj = TestLocalNode::ptr::Create(ae::CreateWith{domain}.with_id(4));
  auto hidden =
      TestLeafNode::ptr::Create(ae::CreateWith{domain}.with_id(5));
  auto owned =
      TestOwnedObject::ptr::Create(ae::CreateWith{domain}.with_id(6));

  leaf_a->label = "A";
  leaf_b->label = "B";
  leaf_a->next = leaf_b;
  leaf_b->next = root;
  owned->leaf_a = leaf_a;
  local_obj->hidden = hidden;
  root->owned = owned;
  root->leaf_b = leaf_b;
  root->local_obj = local_obj;

  auto const discovered = DiscoverSharedGraph(root);
  ExpectExactIds(discovered, {root.id(), leaf_a.id(), leaf_b.id()});
  CHECK(!ContainsId(discovered, owned.id()));
  CHECK(!ContainsId(discovered, local_obj.id()));
  CHECK(!ContainsId(discovered, hidden.id()));
}

void TestChatDiscoveryBeforeAndAfterMessage() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};

  auto graph = examples::BuildSingleClientChatGraph<FakeWindow, FakeWindowPresenter,
                                                    FakeChatPresenter>(
      domain, "Alice");

  auto before = DiscoverSharedGraph(graph.chat);
  ExpectExactIds(before, {graph.chat.id(), graph.local_client.id()});
  CHECK(!ContainsId(before, graph.chat_base.id()));
  CHECK(!ContainsId(before, graph.client_base.id()));
  CHECK(!ContainsId(before, graph.chat_presenter.id()));
  CHECK(!ContainsId(before, graph.window.id()));
  CHECK(!ContainsId(before, graph.window_presenter.id()));
  CHECK(!graph.chat->journal.empty());
  auto join_event = graph.chat->journal.front().event;
  CHECK(!ContainsId(before, join_event.id()));

  graph.chat_presenter->SubmitText("hello");
  auto after = DiscoverSharedGraph(graph.chat);
  ExpectExactIds(after, {graph.chat.id(), graph.local_client.id()});
}

void TestWindowDiscovery() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};

  auto graph = examples::BuildSingleClientChatGraph<FakeWindow, FakeWindowPresenter,
                                                    FakeChatPresenter>(
      domain, "Alice");

  auto discovered = DiscoverSharedGraph(graph.window);
  ExpectExactIds(discovered,
                 {graph.window.id(), graph.chat.id(), graph.local_client.id()});
  CHECK(!ContainsId(discovered, graph.window_presenter.id()));
  CHECK(!ContainsId(discovered, graph.chat_presenter.id()));
  CHECK(!ContainsId(discovered, graph.window_base.id()));
  CHECK(!ContainsId(discovered, graph.chat_base.id()));
  CHECK(!ContainsId(discovered, graph.client_base.id()));
}

void TestTwoClientDiscovery() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};

  auto graph = examples::BuildSingleClientChatGraph<FakeWindow, FakeWindowPresenter,
                                                    FakeChatPresenter>(
      domain, "Alice");

  auto second = Client::ptr::Create(ae::CreateWith{domain});
  second->name = "Bob";
  auto second_base = Client::ptr::Create(ae::CreateWith{domain});
  second->base = second_base;
  second->CaptureBaseState();

  auto join = JoinClientEvent::ptr::Create(ae::CreateWith{domain});
  join->client = second;
  graph.chat->Commit(join);

  auto discovered = DiscoverSharedGraph(graph.chat);
  ExpectExactIds(discovered,
                 {graph.chat.id(), graph.local_client.id(), second.id()});
  CHECK(!ContainsId(discovered, second_base.id()));
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSingleClientChatRegistration();

  apptraverse::test::TestGenericSharedGraphDiscovery();
  apptraverse::test::TestChatDiscoveryBeforeAndAfterMessage();
  apptraverse::test::TestWindowDiscovery();
  apptraverse::test::TestTwoClientDiscovery();
  return 0;
}
