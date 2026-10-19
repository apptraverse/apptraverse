#include "apptraverse/publication_serialization_scope.h"

namespace apptraverse {
namespace {

PublicationSerializationStats g_stats{};

}  // namespace

PublicationSerializationStats& ActivePublicationSerializationStats() {
  return g_stats;
}

void ResetPublicationSerializationStats() { g_stats = {}; }

namespace detail {

void NoteNodeHistorySerialized(std::size_t journal_entries,
                               bool base_snapshot) {
  g_stats.journal_entries_serialized += journal_entries;
  if (base_snapshot) {
    ++g_stats.node_base_snapshots_serialized;
  }
}

void NoteNodeV4PublicationOmitSave() {
  ++g_stats.node_v4_publication_omit_saves;
}

void NoteMaterializedScratchObjectCount(std::size_t count) {
  g_stats.materialized_scratch_object_count = count;
}

void NoteEventRecordVectorSeri(bool materialized_publication_short_circuit) {
  ++g_stats.event_record_vector_seri_calls;
  if (materialized_publication_short_circuit) {
    ++g_stats.event_record_vector_publication_short_circuits;
  }
}

}  // namespace detail

}  // namespace apptraverse
