#include "shared_node_demo_model.h"

#include "apptraverse/object_macros.h"

namespace apptraverse::example::shared_node {
namespace {

APPTRAVERSE_REGISTER(SharedValueNode);
APPTRAVERSE_REGISTER(SetValueEvent);
APPTRAVERSE_REGISTER(Client);

}  // namespace

void SharedValueNode::Apply(SetValueEvent const& event) {
  value = event.value;
  NoteMaterializedChange();
}

void EnsureSharedNodeDemoRegistration() {}

}  // namespace apptraverse::example::shared_node
