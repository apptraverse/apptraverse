#include "apptraverse/shared_graph.h"

#include <algorithm>
#include <cassert>

#include "apptraverse/shared_discovery.h"

namespace apptraverse {

std::vector<Node::ptr> DiscoverSharedGraph(Node::ptr root) {
  assert(root.is_valid());
  root.Load();
  assert(root.is_loaded());

  detail::SharedDiscoveryContext ctx;
  ctx.EnqueueShared(root);

  for (std::size_t i = 0; i < ctx.pending.size(); ++i) {
    auto& node = ctx.pending[i];
    node.Load();
    assert(node.is_loaded());
    node->ReflectForSharedDiscovery(ctx);
  }

  std::vector<Node::ptr> result = ctx.pending;
  std::sort(result.begin(), result.end(),
            [](Node::ptr const& a, Node::ptr const& b) {
              return a.id().id() < b.id().id();
            });
  return result;
}

}  // namespace apptraverse
