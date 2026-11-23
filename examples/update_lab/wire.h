#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
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
  std::string path; // Relative to the published store.
  AE_REFLECT_MEMBERS(id, size, path)
};
// Catalog v4 declares optional executable compatibility for data-only updates.
inline constexpr unsigned kCatalogFormat = 4, kDeliveryProtocol = 2;
inline constexpr unsigned kLatest = UINT32_MAX;
struct Resource {
  std::string locale, full; // An empty component ID places this root in the base image.
  unsigned root, revision;
  AE_REFLECT_MEMBERS(locale, full, root, revision)
};
struct Release {
  std::uint32_t id, executable;
  std::string supply;
  std::vector<Resource> languages;
  AE_REFLECT_MEMBERS(id, executable, supply, languages)
};
struct Catalog {
  std::uint32_t format, generation;
  std::vector<Release> releases;
  std::vector<Component> components;
  std::map<std::string, std::vector<unsigned>> platforms;
  std::map<unsigned, std::vector<unsigned>> compatible_executables;
  AE_REFLECT_MEMBERS(format, generation, releases, components, platforms, compatible_executables)
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
enum LifecycleText : unsigned {
  Refresh = TextCount, UpdateData, UpdateExecutable, InstalledVersion, NewVersion,
  UpToDate, Checking, Percent, LifecycleTextCount
};
// Values 0 and 1 preserve the original single-byte broken-fixture flag.
enum class InstallMode : std::uint8_t { Preferred, BrokenFixture, DataOnly };
struct Intent {
  std::uint32_t release;
  std::string language;
  InstallMode mode{InstallMode::Preferred};
  AE_REFLECT_MEMBERS(release, language, mode)
};
struct InstalledImage {
  Release release;
  std::map<std::string, unsigned> resources; // Includes inactive installed locales.
  AE_REFLECT_MEMBERS(release, resources)
};
struct ResolveRequest {
  std::string platform;
  unsigned current, executable;
  Intent target;
  std::map<std::string, unsigned> installed;
  std::map<std::string, std::uint64_t> received;
  AE_REFLECT_MEMBERS(platform, current, executable, target, installed, received)
};
struct Chain {
  std::string base, target;
  unsigned revision;
  std::vector<std::string> steps;
  AE_REFLECT_MEMBERS(base, target, revision, steps)
};
struct UpdatePlan {
  std::vector<unsigned> available;
  InstalledImage image;
  Intent intent;
  Chain supply;
  std::vector<Chain> resources;
  unsigned executable;
  std::string executable_id;
  std::vector<Component> files;
  std::uint64_t bytes{}, remaining{};
  AE_REFLECT_MEMBERS(available, image, intent, supply, resources, executable,
                     executable_id, files, bytes, remaining)
};
struct UpdateOffer {
  UpdatePlan preferred;
  std::optional<std::uint64_t> data_only_remaining;
  AE_REFLECT_MEMBERS(preferred, data_only_remaining)
};
std::string Resolve(Catalog const&, ResolveRequest const&, UpdatePlan&);
Resource const* FindLanguage(Release const&, std::string const&);
std::string DeltaId(unsigned from, unsigned to, std::string const& locale = {});
std::string ExeId(std::string const& platform, unsigned version);
enum FrameKind : std::uint32_t {
  RequestChunk = 2, Chunk = 4, ServiceError = 5, ResolveUpdate = 6, UpdateResolved = 7,
  PreviewUpdate = 8, UpdateOffered = 9
};
struct Frame {
  std::uint32_t protocol, kind;
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
bool ValidAppId(std::string_view);
fs::path AppStore(fs::path const& root, std::string_view app);
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
