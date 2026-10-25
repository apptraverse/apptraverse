#ifndef APPTRAVERSE_PUBLICATION_SERIALIZATION_SCOPE_H_
#define APPTRAVERSE_PUBLICATION_SERIALIZATION_SCOPE_H_

#include <type_traits>
#include <utility>

#include "aether-objects/obj/domain.h"

#include "apptraverse/graph_copy_policy.h"

namespace apptraverse {

namespace detail {

template <typename Dnv, typename = void>
struct DnvSerializationScope {
  static ae::GraphSerializationScope* Get(Dnv&) { return nullptr; }
};

template <typename Dnv>
struct DnvSerializationScope<
    Dnv, std::void_t<decltype(std::declval<Dnv>().visitor.bin_archive.buffer()
                                  .domain_graph)>> {
  static ae::DomainGraph* GetGraph(Dnv& dnv) {
    return dnv.visitor.bin_archive.buffer().domain_graph;
  }

  static ae::GraphSerializationScope* Get(Dnv& dnv) {
    ae::DomainGraph* graph = GetGraph(dnv);
    return graph != nullptr ? &graph->serialization_scope : nullptr;
  }
};

template <typename Dnv>
bool OmitNodeHistoryForPublication(Dnv& dnv) {
  ae::GraphSerializationScope* scope =
      DnvSerializationScope<std::decay_t<Dnv>>::Get(dnv);
  return scope != nullptr &&
         *scope == ae::GraphSerializationScope::MaterializedPublication;
}

}  // namespace detail

}  // namespace apptraverse

#endif  // APPTRAVERSE_PUBLICATION_SERIALIZATION_SCOPE_H_
