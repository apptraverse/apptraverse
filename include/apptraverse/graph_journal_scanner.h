#ifndef APPTRAVERSE_GRAPH_JOURNAL_SCANNER_H_
#define APPTRAVERSE_GRAPH_JOURNAL_SCANNER_H_

#include <cassert>
#include <functional>
#include <set>
#include <utility>

#include "apptraverse/event_record.h"
#include "apptraverse/node.h"
#include "apptraverse/object_graph_traversal.h"

namespace apptraverse {
namespace detail {

class PendingJournalTraversal : public ObjectGraphTraversal {
 public:
  explicit PendingJournalTraversal(
      std::function<void(Node&, EventRecord&)> pending_visitor)
      : pending_visitor_{std::move(pending_visitor)} {}

 protected:
  void OnNode(Node& node) override {
    if (!visited_nodes_.insert(&node).second) {
      return;
    }

    for (auto& record : node.journal) {
      if (record.delivery_status != DeliveryStatus::kPending) {
        continue;
      }
      std::invoke(pending_visitor_, node, record);
    }
  }

 private:
  std::set<Node*> visited_nodes_;
  std::function<void(Node&, EventRecord&)> pending_visitor_;
};

}  // namespace detail

class GraphJournalScanner {
 public:
  template <typename RootPtr, typename PendingVisitor>
  void VisitPending(RootPtr& root, PendingVisitor&& pending_visitor) const {
    assert(root.is_valid());
    assert(root.is_loaded());

    std::function<void(Node&, EventRecord&)> callback =
        [&](Node& node, EventRecord& record) {
          std::invoke(pending_visitor, node, record);
        };

    detail::PendingJournalTraversal traversal{std::move(callback)};
    traversal.Traverse(root);
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_GRAPH_JOURNAL_SCANNER_H_
