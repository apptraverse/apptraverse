#include "apptraverse/shared_graph.h"

#include <utility>

#include "aether-miscpp/serialization/binary_archive.h"
#include "aether-objects/obj/domain.h"
#include "apptraverse/closed_event_graph.h"

namespace apptraverse {

bool SerializeSharedGraph(ae::Obj const& root,
                          std::vector<std::uint8_t>& bytes) {
  if (root.domain == nullptr) {
    return false;
  }
  ae::RamDomainStorage scratch;
  ae::Domain scratch_domain{scratch};
  ae::DomainGraph graph{&scratch_domain,
                        ae::GraphSerializationScope::NetworkShared};
  graph.force_materialize = true;
  auto const saved = graph.SaveRoot(root.domain->Find(root.obj_id), root.obj_id);
  if (saved.IsErr()) {
    return false;
  }
  SharedGraphSnapshot snapshot{root.obj_id, std::move(scratch.state)};
  std::vector<std::uint8_t> frozen;
  ae::seri::BinaryVectorBuffer buffer{frozen};
  ae::seri::BinaryArchive archive{buffer};
  if (archive.Save(snapshot).IsErr()) {
    return false;
  }
  bytes = std::move(frozen);
  return true;
}

bool DeserializeSharedGraph(std::vector<std::uint8_t> const& bytes,
                            SharedGraphSnapshot& snapshot) {
  auto input = bytes;
  ae::seri::BinaryVectorBuffer buffer{input};
  ae::seri::BinaryArchive archive{buffer};
  SharedGraphSnapshot parsed;
  if (archive.Load(parsed).IsErr() ||
      archive.buffer().read_position() != input.size() ||
      !parsed.root_id.is_valid()) {
    return false;
  }
  ae::RamDomainStorage storage;
  storage.state = parsed.layers;
  if (!ValidateStoredClassChains(storage) ||
      storage.Enumerate(parsed.root_id).empty()) {
    return false;
  }
  snapshot = std::move(parsed);
  return true;
}

}  // namespace apptraverse
