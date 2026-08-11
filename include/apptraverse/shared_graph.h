#ifndef APPTRAVERSE_SHARED_GRAPH_H_
#define APPTRAVERSE_SHARED_GRAPH_H_

#include <vector>

#include "apptraverse/node.h"

namespace apptraverse {

// Discover the shared Node subgraph reachable from root.
// Includes root and every Node targeted by a SharedPtr edge.
// LocalPtr edges are never followed. Result is sorted by ObjId.
std::vector<Node::ptr> DiscoverSharedGraph(Node::ptr root);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_GRAPH_H_
