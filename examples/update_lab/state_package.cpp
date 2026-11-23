#include "state_package.h"
#include <cstdlib>
#include <LzmaDec.h>
#include <LzmaEnc.h>

namespace update_lab {
namespace {
void* Allocate(ISzAllocPtr, size_t size) { return std::malloc(size); }
void Free(ISzAllocPtr, void* memory) { std::free(memory); }
constexpr ISzAlloc allocator{Allocate, Free};

// The first five bytes are the SDK's decoder properties. The remaining bytes
// are one raw LZMA1 stream with an end marker and no container or checksum.
Bytes Compress(Bytes const& input) {
  if (input.size() > kMaxBlobSize) return {};
  CLzmaEncProps props;
  LzmaEncProps_Init(&props);
  props.level = 9;
  props.fb = 273;
  props.numThreads = 1;
  props.reduceSize = input.size(); // No match can reach beyond the entire input.
  SizeT capacity = input.size() + input.size() / 3 + 128;
  Bytes output(LZMA_PROPS_SIZE + capacity);
  SizeT properties = LZMA_PROPS_SIZE;
  Byte empty = 0;
  auto status = LzmaEncode(output.data() + properties, &capacity,
      input.empty() ? &empty : input.data(), input.size(), &props,
      output.data(), &properties, 1, nullptr, &allocator, &allocator);
  if (status != SZ_OK || properties != LZMA_PROPS_SIZE) return {};
  output.resize(properties + capacity);
  return output;
}
bool Expand(Bytes const& input, std::uint64_t size, Bytes& out) {
  if (size > kMaxBlobSize || input.size() <= LZMA_PROPS_SIZE) return false;
  SizeT consumed = input.size() - LZMA_PROPS_SIZE, produced = static_cast<SizeT>(size);
  Bytes decoded(produced);
  Byte empty = 0;
  ELzmaStatus status;
  auto result = LzmaDecode(decoded.empty() ? &empty : decoded.data(), &produced,
      input.data() + LZMA_PROPS_SIZE, &consumed, input.data(), LZMA_PROPS_SIZE,
      LZMA_FINISH_END, &status, &allocator);
  if (result != SZ_OK || status != LZMA_STATUS_FINISHED_WITH_MARK ||
      consumed != input.size() - LZMA_PROPS_SIZE || produced != size) return false;
  out = std::move(decoded);
  return true;
}
}
Bytes PackageRaw(Bytes const& bytes, unsigned kind, unsigned from, unsigned to) {
  auto compressed = Compress(bytes);
  if (compressed.empty() || compressed.size() > kMaxBlobSize - 24) return {};
  return Encode(StatePackage{kind, from, to, bytes.size(), std::move(compressed)});
}
bool UnpackRaw(Bytes const& bytes, StatePackage& package, Bytes& out) {
  return Decode(bytes, package) && Expand(package.compressed, package.raw_size, out);
}
Bytes FullState(ae::RamDomainStorage const& storage, unsigned version) {
  Bytes bytes;
  if (!apptraverse::SerializeUserStorage(storage, bytes)) return {};
  return PackageRaw(bytes, 0, version, version);
}
Bytes DeltaState(ae::RamDomainStorage const& before, ae::RamDomainStorage const& after,
                 unsigned from, unsigned to) {
  StateDelta delta;
  for (auto const& [id, classes] : after.state) {
    auto old = before.state.find(id);
    if (!classes) {
      if (old == before.state.end() || old->second) delta.changed[id] = std::nullopt;
      continue;
    }
    if (old == before.state.end() || !old->second) delta.changed[id].emplace();
    for (auto const& [cls, versions] : *classes) {
      if (versions.empty() && (old == before.state.end() || !old->second || !old->second->contains(cls))) {
        if (!delta.changed[id]) delta.changed[id].emplace();
        (*delta.changed[id])[cls];
      }
      for (auto const& [version, bytes] : versions) {
        bool equal = old != before.state.end() && old->second &&
            old->second->contains(cls) && old->second->at(cls).contains(version) &&
            old->second->at(cls).at(version) == bytes;
        if (!equal) {
          if (!delta.changed[id]) delta.changed[id].emplace();
          (*delta.changed[id])[cls][version] = bytes;
        }
      }
    }
  }
  for (auto const& [id, classes] : before.state) {
    auto next = after.state.find(id);
    if (next == after.state.end()) { delta.removed_objects.push_back(id); continue; }
    if (!classes || !next->second) continue;
    for (auto const& [cls, versions] : *classes) {
      if (!next->second->contains(cls)) { delta.removed_classes.push_back({id, cls}); continue; }
      for (auto const& [v, bytes] : versions) if (!next->second->at(cls).contains(v))
        delta.removed.push_back({id, cls, v});
    }
  }
  ae::RamDomainStorage metadata;
  metadata.object_scopes = after.object_scopes;
  apptraverse::SerializeUserStorage(metadata, delta.scope_metadata);
  return PackageRaw(Encode(delta), 1, from, to);
}
bool ApplyStatePackage(Bytes const& bytes, ae::RamDomainStorage& storage, unsigned& version) {
  StatePackage package;
  Bytes raw;
  if (!UnpackRaw(bytes, package, raw)) return false;
  ae::RamDomainStorage candidate;
  if (package.kind == 0) {
    if (!apptraverse::DeserializeUserStorage(candidate, raw)) return false;
  } else if (package.kind == 1 && package.from == version) {
    StateDelta delta;
    if (!Decode(raw, delta)) return false;
    candidate.state = storage.state;
    ae::RamDomainStorage metadata;
    if (!apptraverse::DeserializeUserStorage(metadata, delta.scope_metadata)) return false;
    candidate.object_scopes = metadata.object_scopes;
    for (auto const& [id, classes] : delta.changed) {
      if (!classes) { candidate.state[id] = std::nullopt; continue; }
      if (!candidate.state[id]) candidate.state[id].emplace();
      for (auto const& [cls, versions] : *classes) {
        auto& target = (*candidate.state[id])[cls];
        for (auto const& [v, data] : versions) target[v] = data;
      }
    }
    for (auto const& d : delta.removed) {
      auto& classes = candidate.state.at(d.id);
      auto& versions = classes->at(d.class_id);
      versions.erase(d.version);
    }
    for (auto const& d : delta.removed_classes) candidate.state.at(d.id)->erase(d.class_id);
    for (auto const& id : delta.removed_objects) candidate.state.erase(id);
  } else return false;
  storage.state = std::move(candidate.state);
  storage.object_scopes = std::move(candidate.object_scopes);
  version = package.to;
  return true;
}
}
