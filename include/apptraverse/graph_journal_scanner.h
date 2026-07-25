#ifndef APPTRAVERSE_GRAPH_JOURNAL_SCANNER_H_
#define APPTRAVERSE_GRAPH_JOURNAL_SCANNER_H_

#include <cassert>
#include <functional>
#include <set>
#include <utility>

#include "aether/obj/obj_id.h"

#include "apptraverse/event_record.h"
#include "apptraverse/node.h"
#include "apptraverse/object_graph_traversal.h"

namespace apptraverse {
namespace detail {

class PendingJournalTraversal : public ObjectGraphTraversal {
 public:
  PendingJournalTraversal(
      ae::ObjId recipient,
      std::function<void(Node&, EventRecord&, EventRecipientState&)>
          pending_visitor)
      : recipient_{recipient}, pending_visitor_{std::move(pending_visitor)} {
    assert(recipient_.IsValid());
  }

 protected:
  void OnNode(Node& node) override {
    if (!visited_nodes_.insert(&node).second) {
      return;
    }

    for (auto& record : node.journal) {
      if (record.origin != EventRecordOrigin::kLocal) {
        continue;
      }

      auto* recipient_state = record.FindRecipient(recipient_);
      if (recipient_state == nullptr) {
        continue;
      }
      if (recipient_state->delivery_status != DeliveryStatus::kPending) {
        continue;
      }

      std::invoke(pending_visitor_, node, record, *recipient_state);
    }
  }

 private:
  ae::ObjId recipient_;
  std::set<Node*> visited_nodes_;
  std::function<void(Node&, EventRecord&, EventRecipientState&)>
      pending_visitor_;
};

}  // namespace detail

class GraphJournalScanner {
 public:
  template <typename RootPtr, typename PendingVisitor>
  void VisitPending(RootPtr& root, ae::ObjId recipient,
                    PendingVisitor&& pending_visitor) const {
    assert(root.is_valid());
    assert(root.is_loaded());
    assert(recipient.IsValid());

    std::function<void(Node&, EventRecord&, EventRecipientState&)> callback =
        [&](Node& node, EventRecord& record,
            EventRecipientState& recipient_state) {
          std::invoke(pending_visitor, node, record, recipient_state);
        };

    detail::PendingJournalTraversal traversal{recipient, std::move(callback)};
    traversal.Traverse(root);
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_GRAPH_JOURNAL_SCANNER_H_
