#include "apptraverse/graph_copy_policy.h"

#include <cassert>
#include <unordered_map>

namespace apptraverse {
namespace {

std::unordered_map<ae::DomainGraph const*, GraphCopyPolicy::Scope>&
ActiveScopes() {
  static std::unordered_map<ae::DomainGraph const*, GraphCopyPolicy::Scope>
      scopes;
  return scopes;
}

}  // namespace

GraphCopyPolicy::Active::Active(ae::DomainGraph const& graph, Scope scope)
    : graph_{&graph} {
  auto& scopes = ActiveScopes();
  assert(scopes.find(graph_) == scopes.end() &&
         "DomainGraph already has an active GraphCopyPolicy");
  scopes.emplace(graph_, scope);
}

GraphCopyPolicy::Active::~Active() {
  auto& scopes = ActiveScopes();
  auto const it = scopes.find(graph_);
  assert(it != scopes.end());
  scopes.erase(it);
}

GraphCopyPolicy::Scope GraphCopyPolicy::ScopeFor(
    ae::DomainGraph const* graph) {
  if (graph == nullptr) {
    return Scope::LocalPersistent;
  }
  auto const& scopes = ActiveScopes();
  auto const it = scopes.find(graph);
  if (it == scopes.end()) {
    return Scope::LocalPersistent;
  }
  return it->second;
}

}  // namespace apptraverse
