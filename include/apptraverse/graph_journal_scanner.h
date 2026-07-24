#ifndef APPTRAVERSE_GRAPH_JOURNAL_SCANNER_H_
#define APPTRAVERSE_GRAPH_JOURNAL_SCANNER_H_

#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <type_traits>
#include <utility>
#include <vector>

#include "aether-miscpp/reflect/domain_visitor.h"

#include "apptraverse/event.h"
#include "apptraverse/event_record.h"
#include "apptraverse/node.h"

namespace apptraverse {
namespace detail {

struct EventScanHandler {
  std::uint32_t class_id{};
  virtual ~EventScanHandler() = default;
  virtual void VisitFields(Event& event,
                           ae::reflect::CycleDetector& cycle_detector) const = 0;
};

struct JournalScanContext {
  std::set<Node*> visited_nodes;
  std::function<void(Node&, EventRecord&)> pending_visitor;
};

inline JournalScanContext*& ActiveJournalScanContext() {
  static thread_local JournalScanContext* context = nullptr;
  return context;
}

inline std::vector<std::unique_ptr<EventScanHandler>>& EventScanHandlers() {
  static std::vector<std::unique_ptr<EventScanHandler>> handlers;
  return handlers;
}

inline EventScanHandler const* FindEventScanHandler(std::uint32_t class_id) {
  for (auto const& handler : EventScanHandlers()) {
    if (handler->class_id == class_id) {
      return handler.get();
    }
  }
  return nullptr;
}

template <typename ConcreteEvent>
struct TypedEventScanHandler final : EventScanHandler {
  TypedEventScanHandler() { class_id = ConcreteEvent::kClassId; }

  void VisitFields(Event& event,
                   ae::reflect::CycleDetector& cycle_detector) const override {
    auto* context = ActiveJournalScanContext();
    assert(context != nullptr);

    auto graph_visitor = [&](auto& value) -> bool {
      using Value = std::remove_cvref_t<decltype(value)>;

      if constexpr (std::is_base_of_v<Node, Value> && !std::is_pointer_v<Value>) {
        auto* node = static_cast<Node*>(&value);
        if (context->visited_nodes.insert(node).second) {
          for (auto& record : node->journal) {
            if (record.delivery_status != DeliveryStatus::kPending) {
              continue;
            }
            std::invoke(context->pending_visitor, *node, record);
          }
        }
      }

      return true;
    };

    using DeepVisitor =
        ae::reflect::DomainNodeVisitor<decltype(graph_visitor),
                                       ae::reflect::VisitPolicy::kDeep>;
    ae::reflect::NodeVisitor<ConcreteEvent>{}.Visit(
        static_cast<ConcreteEvent&>(event), cycle_detector,
        DeepVisitor{std::move(graph_visitor)});
  }
};

template <typename ConcreteEvent>
void RegisterEventScanHandler() {
  auto const class_id = ConcreteEvent::kClassId;
  if (FindEventScanHandler(class_id) != nullptr) {
    return;
  }
  EventScanHandlers().push_back(
      std::make_unique<TypedEventScanHandler<ConcreteEvent>>());
}

}  // namespace detail

// Registers ConcreteEvent so journal Event::ptr values can deep-visit fields
// declared on the concrete event type (EventRecord stores Event::ptr).
template <typename ConcreteEvent>
struct EnableEventGraphScan {
  EnableEventGraphScan() { detail::RegisterEventScanHandler<ConcreteEvent>(); }
};

class GraphJournalScanner {
 public:
  template <typename RootPtr, typename PendingVisitor>
  void VisitPending(RootPtr& root, PendingVisitor&& pending_visitor) const {
    assert(root.is_valid());
    assert(root.is_loaded());

    detail::JournalScanContext context;
    context.pending_visitor = [&](Node& node, EventRecord& record) {
      std::invoke(pending_visitor, node, record);
    };

    auto graph_visitor = [&](auto& value) -> bool {
      using Value = std::remove_cvref_t<decltype(value)>;

      if constexpr (std::is_base_of_v<Node, Value> && !std::is_pointer_v<Value>) {
        auto* node = static_cast<Node*>(&value);
        if (context.visited_nodes.insert(node).second) {
          for (auto& record : node->journal) {
            if (record.delivery_status != DeliveryStatus::kPending) {
              continue;
            }
            std::invoke(context.pending_visitor, *node, record);
          }
        }
      }

      return true;
    };

    using DeepVisitor =
        ae::reflect::DomainNodeVisitor<decltype(graph_visitor),
                                       ae::reflect::VisitPolicy::kDeep>;

    detail::ActiveJournalScanContext() = &context;
    ae::reflect::DomainVisit(root, DeepVisitor{std::move(graph_visitor)});
    detail::ActiveJournalScanContext() = nullptr;
  }
};

}  // namespace apptraverse

namespace ae::reflect {

template <>
struct NodeVisitor<apptraverse::Event> {
  using Policy = AnyPolicyMatch;

  template <typename Visitor>
  void Visit(apptraverse::Event& event, CycleDetector& cycle_detector,
             Visitor&& visitor) const {
    // Concrete-field expansion is only active during GraphJournalScanner
    // VisitPending. Outside that scope (e.g. Domain save/load) keep Event's
    // own reflected layer so serialization keeps working.
    if (apptraverse::detail::ActiveJournalScanContext() != nullptr) {
      if (auto const* handler =
              apptraverse::detail::FindEventScanHandler(event.GetClassId());
          handler != nullptr) {
        handler->VisitFields(event, cycle_detector);
        return;
      }
    }

    Reflection{event}.Apply([&](auto&... fields) {
      (ApplyVisitor(fields, cycle_detector, std::forward<Visitor>(visitor)),
       ...);
    });
  }

  template <typename Visitor>
  void Visit(apptraverse::Event const& event, CycleDetector& cycle_detector,
             Visitor&& visitor) const {
    Visit(const_cast<apptraverse::Event&>(event), cycle_detector,
          std::forward<Visitor>(visitor));
  }
};

}  // namespace ae::reflect

#endif  // APPTRAVERSE_GRAPH_JOURNAL_SCANNER_H_
