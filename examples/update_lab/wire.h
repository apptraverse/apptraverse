#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "aether-miscpp/reflect/reflect.h"
#include "aether-miscpp/serialization/binary_archive.h"
#include "aether-miscpp/serialization/serialization.h"

namespace update_lab {
using Bytes = std::vector<std::uint8_t>;
namespace fs = std::filesystem;
inline constexpr std::uint64_t kChunkSize = 65536;
inline constexpr std::uint64_t kMaxBlobSize = 256 * 1024 * 1024;
struct Component {
  std::string id;
  std::uint64_t size;
  AE_REFLECT_MEMBERS(id, size)
};
struct Release {
  std::uint32_t id, executable;
  std::string supply;
  std::vector<std::string> languages;
  AE_REFLECT_MEMBERS(id, executable, supply, languages)
};
struct Catalog {
  std::uint32_t format, generation;
  std::vector<Release> releases;
  std::vector<Component> components;
  AE_REFLECT_MEMBERS(format, generation, releases, components)
};
struct LanguagePack {
  std::string locale;
  std::vector<std::string> text;
  Bytes payload; // Deliberately sizable fixture: transfer/resume is observable.
  AE_REFLECT_MEMBERS(locale, text, payload)
};
enum Text : unsigned {
  Title, Greeting, Language, Version, Note, ApplyNote, AddClone, Clone,
  Downloading, Ready, Failure, Slow, Restart, S1Label, Broken, Export, Import,
  TextCount
};
struct Intent {
  std::uint32_t release;
  std::string language;
  bool broken;
  AE_REFLECT_MEMBERS(release, language, broken)
};
struct Frame {
  std::uint32_t protocol, kind; // 1/catalog request, 2/chunk request, 3/4 replies, 5/error
  std::string id;
  std::uint64_t chunk;
  Bytes payload;
  std::uint64_t request{0};
  AE_REFLECT_MEMBERS(protocol, kind, id, chunk, payload, request)
};
struct FramePiece {
  std::uint64_t request;
  std::uint32_t size, offset;
  Bytes bytes;
  AE_REFLECT_MEMBERS(request, size, offset, bytes)
};
// SDK messages are independently delivered. Offsets permit ordering and
// duplicate delivery to vary, without any checksum or digest.
class FrameAssembler {
 public:
  std::optional<Frame> Receive(FramePiece const&);
  std::size_t buffered() const;
  void Clear() { pending_.clear(); }
 private:
  struct Assembly { Bytes bytes; std::vector<bool> seen; std::size_t received{0}; };
  std::map<std::uint64_t, Assembly> pending_;
};
template<class T> Bytes Encode(T const& value) {
  Bytes bytes;
  ae::seri::BinaryVectorBuffer buffer{bytes};
  ae::seri::BinaryArchive archive{std::move(buffer), static_cast<std::size_t>(kMaxBlobSize)};
  if (archive.Save(value).IsErr()) return {};
  return bytes;
}
template<class T> bool Decode(Bytes bytes, T& value) {
  ae::seri::BinaryVectorBuffer buffer{bytes};
  ae::seri::BinaryArchive archive{std::move(buffer), static_cast<std::size_t>(kMaxBlobSize)};
  return archive.Load(value).IsOk();
}
bool ReadFile(fs::path const&, Bytes&, std::uint64_t max = kMaxBlobSize);
bool AtomicWrite(fs::path const&, Bytes const&);
bool CopyAtomic(fs::path const&, fs::path const&);
bool ReadCatalog(Bytes const&, Catalog&);
Component const* FindComponent(Catalog const&, std::string const&);
Release const* FindRelease(Catalog const&, unsigned);
bool ValidPack(LanguagePack const&, std::string const&);
std::string Platform();
std::string BinaryName();
std::string LanguageId(unsigned, std::string const&);
std::string ExecutableId(unsigned);
std::string FileId(std::string id);
[[noreturn]] void Fail(std::string const&);
class FileLock {
 public:
  explicit FileLock(fs::path const&);
  ~FileLock();
  bool acquired() const { return acquired_; }
 private:
  std::intptr_t handle_{-1};
  bool acquired_{false};
};
}
