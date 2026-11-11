#include <cstdlib>
#include <iostream>

#include "aether-miscpp/reflect/reflect.h"
#include "aether-objects/ptr/ptr.h"

namespace {
struct Node {
  explicit Node(int& destroyed) : destroyed{&destroyed} {}
  ~Node() { ++*destroyed; }
  ae::Ptr<Node> child;
  ae::Ptr<Node> owner;
  int* destroyed;
  AE_REFLECT_MEMBERS(child, owner)
};

void Check(bool condition, char const* message) {
  if (!condition) {
    std::cerr << "aether_ptr_releasing_edge_test FAIL: " << message << '\n';
    std::exit(1);
  }
}

void TestReleasingMemberEdge(bool second_owner_edge, bool external_keepalive) {
  int owners_destroyed = 0;
  int children_destroyed = 0;
  auto owner = ae::MakePtr<Node>(owners_destroyed);
  auto child = ae::MakePtr<Node>(children_destroyed);
  child->child = child;  // The child has its own cyclic subgraph.
  child->owner = owner;
  owner->child = child;
  if (second_owner_edge) { owner->owner = child; }
  ae::Ptr<Node> keepalive;
  if (external_keepalive) { keepalive = child; }
  child = {};
  Check(children_destroyed == 0, "owner still retains the child");

  // The traversal comes back through child.owner to this exact releasing
  // member. It is counted once at the root and must not be a surviving edge.
  owner->child = {};
  Check(owners_destroyed == 0, "the external owner must remain alive");
  if (second_owner_edge || external_keepalive) {
    Check(children_destroyed == 0, "a distinct surviving edge must retain child");
  } else {
    Check(children_destroyed == 1, "releasing last owner edge must destroy cycle");
  }
  if (second_owner_edge) {
    owner->owner = {};
    Check(children_destroyed == (external_keepalive ? 0 : 1),
          "releasing the second member edge respects external keepalive");
  }
  keepalive = {};
  Check(children_destroyed == 1, "child is destroyed exactly once");
  Check(owners_destroyed == 0, "owner survives child destruction");
  owner = {};
  Check(owners_destroyed == 1, "owner is destroyed exactly once");
}
}  // namespace

int main() {
  TestReleasingMemberEdge(false, false);
  TestReleasingMemberEdge(true, false);
  TestReleasingMemberEdge(false, true);
  TestReleasingMemberEdge(true, true);
  std::cout << "AETHER_PTR_RELEASING_EDGE ok\n";
}
