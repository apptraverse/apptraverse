#include "state_package.h"
#include "object_storage_compression.h"

namespace update_lab {
namespace {
Bytes Compress(Bytes const& input) {
  Bytes output;
  if (input.size() > kMaxBlobSize ||
      !apptraverse::storage_compression::CompressLzma(input, output)) return {};
  return output;
}

bool Expand(Bytes const& input, std::uint64_t size, Bytes& out) {
  if (size > kMaxBlobSize) return false;
  Bytes decoded(static_cast<std::size_t>(size));
  if (!apptraverse::storage_compression::ExpandLzma(input, decoded)) return false;
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
  if (!apptraverse::SerializeNativeStorage(storage, bytes)) return {};
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
  apptraverse::SerializeNativeStorage(metadata, delta.scope_metadata);
  return PackageRaw(Encode(delta), 1, from, to);
}
bool ApplyStatePackage(Bytes const& bytes, ae::RamDomainStorage& storage, unsigned& version) {
  StatePackage package;
  Bytes raw;
  if (!UnpackRaw(bytes, package, raw)) return false;
  ae::RamDomainStorage candidate;
  if (package.kind == 0) {
    if (!apptraverse::DeserializeNativeStorage(candidate, raw)) return false;
  } else if (package.kind == 1 && package.from == version) {
    StateDelta delta;
    if (!Decode(raw, delta)) return false;
    candidate.state = storage.state;
    ae::RamDomainStorage metadata;
    if (!apptraverse::DeserializeNativeStorage(metadata, delta.scope_metadata)) return false;
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
