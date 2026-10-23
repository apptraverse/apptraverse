#ifndef APPTRAVERSE_PUBLICATION_SERIALIZATION_SCOPE_H_
#define APPTRAVERSE_PUBLICATION_SERIALIZATION_SCOPE_H_

#include <cstdint>

#include "aether-objects/obj/domain.h"

#include "apptraverse/graph_copy_policy.h"

namespace apptraverse {

struct PublicationSerializationStats {
  std::uint64_t journal_entries_serialized{0};
  std::uint64_t node_base_snapshots_serialized{0};
  std::uint64_t journal_replay_calls{0};
  std::uint64_t node_publication_omit_saves{0};
  std::uint64_t materialized_scratch_object_count{0};
  std::uint64_t materialized_historical_event_objects{0};
  std::uint64_t materialized_scratch_layer_bytes{0};
  std::uint64_t event_record_vector_seri_calls{0};
  std::uint64_t event_record_vector_publication_short_circuits{0};
  std::uint64_t last_materialized_save_micros{0};
  std::uint64_t last_materialized_post_save_micros{0};
};

PublicationSerializationStats& ActivePublicationSerializationStats();
void ResetPublicationSerializationStats();

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

void NoteNodeHistorySerialized(std::size_t journal_entries,
                               bool base_snapshot);

void NoteNodePublicationOmitSave();

void NoteMaterializedScratchObjectCount(std::size_t count);

void NoteEventRecordVectorSeri(bool materialized_publication_short_circuit);

}  // namespace detail

}  // namespace apptraverse

#endif  // APPTRAVERSE_PUBLICATION_SERIALIZATION_SCOPE_H_
