#include "apptraverse/shared_network_graph.h"

#include <cassert>
#include <cstddef>
#include <utility>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/registry.h"

namespace apptraverse {
namespace {

// Serialize the live network-shared view of root into a scratch RAM storage.
// LocalPtr edges become empty/default; the source storage is untouched.
void BuildNetworkSharedScratch(ae::Obj const& root,
                               ae::RamDomainStorage& scratch) {
  assert(root.domain != nullptr);

  ae::Domain scratch_domain{scratch};
  ae::DomainGraph graph{&scratch_domain,
                        ae::GraphSerializationScope::NetworkShared};

  auto ptr = root.domain->Find(root.obj_id);
  assert(ptr);
  auto* factory = ae::Registry::GetRegistry().FindFactory(root.GetClassId());
  assert(factory != nullptr);
  assert(factory->save != nullptr);
  factory->save(&graph, ptr, root.obj_id);
}

void AppendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

bool ReadU32(std::vector<std::uint8_t> const& in, std::size_t& pos,
             std::uint32_t& value) {
  if (pos + 4 > in.size()) {
    return false;
  }
  value = (static_cast<std::uint32_t>(in[pos]) << 24U) |
          (static_cast<std::uint32_t>(in[pos + 1]) << 16U) |
          (static_cast<std::uint32_t>(in[pos + 2]) << 8U) |
          static_cast<std::uint32_t>(in[pos + 3]);
  pos += 4;
  return true;
}

void TransferRamObject(ae::RamDomainStorage const& src, ae::ObjId obj_id,
                       ae::IDomainStorage& dst) {
  auto const it = src.state.find(obj_id);
  if (it == src.state.end() || !it->second.has_value()) {
    return;
  }
  for (auto const& [class_id, versions] : *it->second) {
    for (auto const& [version, data] : versions) {
      auto writer = dst.Store(ae::DomainQuery{obj_id, class_id, version});
      assert(writer != nullptr);
      if (!data.empty()) {
        auto const result = writer->Write(
            ae::seri::DataWriteTag{data.data(), data.size()});
        assert(result);
        (void)result;
      }
    }
  }
}

}  // namespace

void CopyNetworkSharedObjectGraph(ae::Obj const& root,
                                  ae::IDomainStorage& target_storage) {
  ae::RamDomainStorage scratch;
  BuildNetworkSharedScratch(root, scratch);

  for (auto const& [obj_id, classes] : scratch.state) {
    if (!classes.has_value()) {
      continue;
    }
    TransferRamObject(scratch, obj_id, target_storage);
  }
}

void CopySharedNetworkGraph(SharedNode::ptr source,
                            ae::Domain& target_domain,
                            ae::IDomainStorage& target_storage) {
  assert(source.is_valid());
  assert(source.is_loaded());
  (void)target_domain;
  CopyNetworkSharedObjectGraph(*source, target_storage);
}

std::vector<std::uint8_t> SerializeNetworkSharedObjectGraph(
    ae::Obj const& root) {
  ae::RamDomainStorage scratch;
  BuildNetworkSharedScratch(root, scratch);

  std::vector<std::uint8_t> out;
  std::uint32_t object_count = 0;
  for (auto const& [obj_id, classes] : scratch.state) {
    if (classes.has_value()) {
      ++object_count;
    }
  }
  AppendU32(out, object_count);

  for (auto const& [obj_id, classes] : scratch.state) {
    if (!classes.has_value()) {
      continue;
    }
    AppendU32(out, obj_id.id());
    AppendU32(out, static_cast<std::uint32_t>(classes->size()));
    for (auto const& [class_id, versions] : *classes) {
      AppendU32(out, class_id);
      AppendU32(out, static_cast<std::uint32_t>(versions.size()));
      for (auto const& [version, data] : versions) {
        out.push_back(version);
        AppendU32(out, static_cast<std::uint32_t>(data.size()));
        out.insert(out.end(), data.begin(), data.end());
      }
    }
  }
  return out;
}

bool ImportObjectGraphPayload(std::vector<std::uint8_t> const& payload,
                              ae::IDomainStorage& target_storage) {
  std::size_t pos = 0;
  std::uint32_t object_count = 0;
  if (!ReadU32(payload, pos, object_count)) {
    return false;
  }
  for (std::uint32_t object = 0; object < object_count; ++object) {
    std::uint32_t obj_id = 0;
    std::uint32_t class_count = 0;
    if (!ReadU32(payload, pos, obj_id) || !ReadU32(payload, pos, class_count)) {
      return false;
    }
    for (std::uint32_t klass = 0; klass < class_count; ++klass) {
      std::uint32_t class_id = 0;
      std::uint32_t version_count = 0;
      if (!ReadU32(payload, pos, class_id) ||
          !ReadU32(payload, pos, version_count)) {
        return false;
      }
      for (std::uint32_t version_index = 0; version_index < version_count;
           ++version_index) {
        if (pos >= payload.size()) {
          return false;
        }
        auto const version = payload[pos++];
        std::uint32_t size = 0;
        if (!ReadU32(payload, pos, size) || pos + size > payload.size()) {
          return false;
        }
        auto writer = target_storage.Store(
            ae::DomainQuery{ae::ObjId{obj_id}, class_id, version});
        assert(writer != nullptr);
        if (size != 0) {
          auto const result = writer->Write(
              ae::seri::DataWriteTag{payload.data() + pos, size});
          assert(result);
          (void)result;
        }
        pos += size;
      }
    }
  }
  return pos == payload.size();
}

}  // namespace apptraverse
