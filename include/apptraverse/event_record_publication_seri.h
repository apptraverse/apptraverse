#ifndef APPTRAVERSE_EVENT_RECORD_PUBLICATION_SERI_H_
#define APPTRAVERSE_EVENT_RECORD_PUBLICATION_SERI_H_

#include "aether-miscpp/serialization/details/binary_archive.h"
#include "aether-miscpp/serialization/details/binary_std_serializers.h"
#include "aether-miscpp/serialization/details/serializer.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/event_record.h"
#include "apptraverse/graph_copy_policy.h"
#include "apptraverse/publication_serialization_scope.h"

namespace ae::seri {

namespace detail {

inline bool IsMaterializedPublicationScope(
    ae::DomainGraph const* graph) noexcept {
  return graph != nullptr &&
         graph->serialization_scope ==
             ae::GraphSerializationScope::MaterializedPublication;
}

}  // namespace detail

template <typename B>
struct Serializer<BinaryArchive<B>, std::vector<apptraverse::EventRecord>> {
  using Archive = BinaryArchive<B>;
  using Vec = std::vector<apptraverse::EventRecord>;

  SeriResult Seri(Archive& archive, Meta<Vec const> const& val) const {
    bool const short_circuit = detail::IsMaterializedPublicationScope(
        archive.buffer().domain_graph);
    if (short_circuit) {
      std::size_t const zero = 0;
      return archive.buffer().Write(SizeTag{zero});
    }
    std::size_t const size = val.value.size();
    TRY_RESULT(archive.buffer().Write(SizeTag{size}));
    for (auto const& elem : val.value) {
      TRY_RESULT(archive.Save(elem));
    }
    return Ok{good};
  }

  SeriResult Deseri(Archive& archive, Meta<Vec> val) const {
    std::size_t size{};
    TRY_RESULT(archive.buffer().Read(SizeTag{size}));
    TRY_RESULT(binary_std_serializers_internal::CheckContainerLoadSize(
        archive, size));
    val.value.resize(size);
    for (auto& elem : val.value) {
      TRY_RESULT(archive.Load(elem));
    }
    return Ok{good};
  }
};

}  // namespace ae::seri

#endif  // APPTRAVERSE_EVENT_RECORD_PUBLICATION_SERI_H_
