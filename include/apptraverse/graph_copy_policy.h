#ifndef APPTRAVERSE_GRAPH_COPY_POLICY_H_
#define APPTRAVERSE_GRAPH_COPY_POLICY_H_

#include <cstdint>

#include "aether-objects/obj/domain.h"

namespace apptraverse {

// Explicit per-DomainGraph serialization policy for graph copy/export.
// Bound with Active at the call site — not a process-global mode flag and not
// thread_local. Unbound graphs use LocalPersistent (default Save/Load).
class GraphCopyPolicy {
 public:
  enum class Scope : std::uint8_t {
    LocalPersistent = 0,
    NetworkShared = 1,
  };

  class Active {
   public:
    Active(ae::DomainGraph const& graph, Scope scope);
    ~Active();

    Active(Active const&) = delete;
    Active& operator=(Active const&) = delete;

   private:
    ae::DomainGraph const* graph_;
  };

  static Scope ScopeFor(ae::DomainGraph const* graph);
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_GRAPH_COPY_POLICY_H_
