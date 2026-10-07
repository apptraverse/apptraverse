#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/event_for.h"
#include "apptraverse/event_record.h"
#include "apptraverse/node.h"
#include "apptraverse/shared_event_order.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

class DirectoryProbeNode;
class DirectoryProbeBumpEvent;

class DirectoryProbeNode : public NodeFor<DirectoryProbeNode> {
  APPTRAVERSE_OBJECT(DirectoryProbeNode, Node, 2)

 protected:
  DirectoryProbeNode() = default;

 public:
  explicit DirectoryProbeNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value), AE_MMBR(label))

  template <typename Dnv>
  void Load(ae::Version<2>, Dnv& dnv) {
    dnv(base_, value, label);
  }

  template <typename Dnv>
  void Save(ae::Version<2>, Dnv& dnv) const {
    dnv(base_, value, label);
  }

  std::int32_t value{0};
  std::string label;

  void Apply(DirectoryProbeBumpEvent const& event);

  void InsertAtForTest(std::uint64_t timestamp_us, Event::ptr event) {
    InsertEvent(EventRecord{.event = std::move(event),
                            .identity = {},
                            .order = SharedEventOrder{.timestamp_us = timestamp_us}});
  }
};

class DirectoryProbeBumpEvent
    : public EventFor<DirectoryProbeNode, DirectoryProbeBumpEvent> {
  APPTRAVERSE_OBJECT(DirectoryProbeBumpEvent, Event, 0)

 protected:
  DirectoryProbeBumpEvent() = default;

 public:
  explicit DirectoryProbeBumpEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(delta))

  std::int32_t delta{0};
};

APPTRAVERSE_REGISTER(DirectoryProbeNode);
APPTRAVERSE_REGISTER(DirectoryProbeBumpEvent);

void DirectoryProbeNode::Apply(DirectoryProbeBumpEvent const& event) {
  value += event.delta;
}

constexpr ae::ObjId::Type kProbeNodeId = 100000;
constexpr ae::ObjId::Type kProbeEventId = 100001;

void TestDirectoryDomainStorageConcreteRoundtrip() {
  auto root = std::filesystem::temp_directory_path() /
              "apptraverse_directory_domain_storage_test";
  std::filesystem::remove_all(root);

  {
    DirectoryDomainStorage storage{root};
    ae::Domain domain{storage};
    auto node = DirectoryProbeNode::ptr::Create(
        ae::CreateWith{domain}.with_id(kProbeNodeId));
    node->label = "probe";
    node->value = 1;
    auto event = DirectoryProbeBumpEvent::ptr::Create(
        ae::CreateWith{domain}.with_id(kProbeEventId));
    event->delta = 4;
    node->InsertAtForTest(1, event);
    node.Save();
    CHECK(node->value == 5);
    CHECK(!node->journal.empty());
  }

  {
    DirectoryDomainStorage storage{root};
    ae::Domain domain{storage};
    auto node = DirectoryProbeNode::ptr::Declare(
        ae::CreateWith{domain}.with_id(kProbeNodeId));
    node.Load();
    CHECK(node.is_loaded());
    CHECK(node->value == 5);
    CHECK(node->label == "probe");
    CHECK(!node->journal.empty());
    auto event2 = DirectoryProbeBumpEvent::ptr::Create(
        ae::CreateWith{domain}.with_id(kProbeEventId + 1));
    event2->delta = 2;
    node->InsertAtForTest(2, event2);
    node.Save();
    CHECK(node->value == 7);
  }

  {
    DirectoryDomainStorage storage{root};
    ae::Domain domain{storage};
    auto node = DirectoryProbeNode::ptr::Declare(
        ae::CreateWith{domain}.with_id(kProbeNodeId));
    node.Load();
    CHECK(node->value == 7);
  }

  std::filesystem::remove_all(root);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestDirectoryDomainStorageConcreteRoundtrip();
  std::cout << "directory_domain_storage_test OK\n";
  return 0;
}
