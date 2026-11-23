#include "apptraverse/single_file_user_storage.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif
#include "aether-miscpp/serialization/binary_archive.h"
#include "object_storage_compression.h"

namespace apptraverse {
namespace {
using Scopes = std::map<ae::ObjId, std::array<std::uint32_t, 3>>;
using ae::Error;
using ae::Ok;
using namespace ae::seri;
using storage_compression::Input;
using storage_compression::Output;
constexpr std::size_t kBlockSize = 16 * 1024;
// Identifies the layout only. The codec comes from the build configuration.
constexpr std::array<std::uint8_t, 4> kSnapshotVersion{0xff, 'A', 'P', 1};

template <class Archive, class Finish>
bool ReadArchive(ae::RamDomainStorage& storage, Archive& archive, Finish finish) {
  ae::RamDomainStorage::ObjClassData loaded;
  Scopes scopes;
  if (archive.Load(loaded).IsErr() || archive.Load(scopes).IsErr() || !finish())
    return false;
  std::map<ae::ObjId, ae::ObjectScope> metadata;
  for (auto const& [id, fields] : scopes) {
    metadata.emplace(id, ae::ObjectScope{ae::ObjId{fields[0]},
                                         ae::ObjId{fields[1]}, fields[2] != 0});
  }
  storage.state = std::move(loaded);
  storage.object_scopes = std::move(metadata);
  return true;
}

template <class Archive>
bool WriteArchive(ae::RamDomainStorage const& storage, Archive& archive) {
  Scopes scopes;
  for (auto const& [id, scope] : storage.object_scopes) {
    scopes.emplace(id, std::array<std::uint32_t, 3>{
                           scope.root_id.id(), scope.parent_root_id.id(),
                           scope.is_constant ? 1U : 0U});
  }
  return archive.Save(storage.state).IsOk() && archive.Save(scopes).IsOk();
}

// Supplies the same archive bytes as BinaryVectorBuffer, incrementally. Each
// block carries a pair of little-endian lengths. Equal lengths mean stored
// bytes. A zero pair ends the snapshot.
template <class ReadBytes, class WriteBytes>
class SnapshotBuffer {
 public:
  SnapshotBuffer(ReadBytes read, WriteBytes write)
      : read_(std::move(read)), write_(std::move(write)) {}

  bool StartReading() {
    if (!read_(lengths_)) return false;
    legacy_ = lengths_ != kSnapshotVersion;
    if (legacy_) {
      std::copy(lengths_.begin(), lengths_.end(), block_.begin());
      size_ = lengths_.size();
    }
    return true;
  }

  SeriResult Read(SizeReadTag tag) {
    std::uint32_t size{};
    TRY_RESULT(Read(DataReadTag{&size, sizeof(size)}));
    tag.size = size;
    return Ok{good};
  }

  SeriResult Read(DataReadTag tag) {
    auto remaining = Output{static_cast<std::uint8_t*>(tag.data), tag.size};
    while (!remaining.empty()) {
      if (position_ == size_) {
        if (legacy_) {
          if (!read_(remaining)) return Error{read_eof};
          return Ok{good};
        }
        if (!ReadBlock()) return Error{read_eof};
      }
      auto const count = std::min(remaining.size(), size_ - position_);
      std::memcpy(remaining.data(), block_.data() + position_, count);
      position_ += count;
      remaining = remaining.subspan(count);
    }
    return Ok{good};
  }

  SeriResult Write(SizeWriteTag tag) {
    auto const size = static_cast<std::uint32_t>(tag.size);
    return Write(DataWriteTag{&size, sizeof(size)});
  }

  SeriResult Write(DataWriteTag tag) {
    auto remaining = Input{static_cast<std::uint8_t const*>(tag.data), tag.size};
    while (!remaining.empty()) {
      auto const count = std::min(remaining.size(), block_.size() - size_);
      std::memcpy(block_.data() + size_, remaining.data(), count);
      size_ += count;
      remaining = remaining.subspan(count);
      if (size_ == block_.size() && !WriteBlock()) return Error{write_eof};
    }
    return Ok{good};
  }

  bool StartWriting() { return write_(kSnapshotVersion); }
  bool FinishWriting() {
    std::array<std::uint8_t, 4> end{};
    return (size_ == 0 || WriteBlock()) && write_(end);
  }
  bool FinishReading() {
    if (!legacy_ &&
        (position_ != size_ || !read_(lengths_) ||
         lengths_ != std::array<std::uint8_t, 4>{})) return false;
    return true;
  }
  bool IsLegacy() const { return legacy_; }

 private:
  bool WriteBlock() {
    auto const raw = Input{block_}.first(size_);
    if (!storage_compression::CompressPersistence(raw, packed_)) return false;
    auto const payload = packed_.size() < raw.size() ? Input{packed_} : raw;
    auto const stored = payload.size();
    lengths_ = {static_cast<std::uint8_t>(size_), static_cast<std::uint8_t>(size_ >> 8),
                static_cast<std::uint8_t>(stored), static_cast<std::uint8_t>(stored >> 8)};
    size_ = 0;
    return write_(lengths_) && write_(payload);
  }

  bool ReadBlock() {
    if (!read_(lengths_)) return false;
    auto const expanded = std::size_t{lengths_[0]} + (std::size_t{lengths_[1]} << 8);
    auto const stored = std::size_t{lengths_[2]} + (std::size_t{lengths_[3]} << 8);
    if (stored == 0 || stored > expanded || expanded > block_.size()) return false;
    size_ = expanded;
    position_ = 0;
    auto const output = Output{block_}.first(size_);
    if (expanded == stored) return read_(output);
    packed_.resize(stored);
    return read_(packed_) && storage_compression::ExpandPersistence(packed_, output);
  }

  ReadBytes read_;
  WriteBytes write_;
  std::vector<std::uint8_t> block_ = std::vector<std::uint8_t>(kBlockSize);
  std::array<std::uint8_t, 4> lengths_{};
  std::vector<std::uint8_t> packed_;
  std::size_t size_{};
  std::size_t position_{};
  bool legacy_{};
};

template <class Sink>
bool WriteSnapshot(ae::RamDomainStorage const& storage, Sink sink) {
  BinaryArchive archive{SnapshotBuffer{[](Output) { return false; }, sink}};
  return archive.buffer().StartWriting() && WriteArchive(storage, archive) &&
         archive.buffer().FinishWriting();
}

template <class Source, class AtEnd>
bool ReadSnapshot(ae::RamDomainStorage& storage, Source source, AtEnd at_end) {
  BinaryArchive archive{SnapshotBuffer{source, [](Input) { return false; }}};
  return archive.buffer().StartReading() && ReadArchive(storage, archive, [&] {
    return archive.buffer().FinishReading() &&
           (archive.buffer().IsLegacy() || at_end());
  });
}
}  // namespace

bool SerializeNativeStorage(ae::RamDomainStorage const& storage,
                            std::vector<std::uint8_t>& bytes) {
  std::vector<std::uint8_t> encoded;
  BinaryArchive archive{BinaryVectorBuffer{encoded}};
  if (!WriteArchive(storage, archive)) return false;
  bytes = std::move(encoded);
  return true;
}

bool DeserializeNativeStorage(ae::RamDomainStorage& storage,
                              std::vector<std::uint8_t> const& bytes) {
  auto input = bytes;
  BinaryArchive archive{BinaryVectorBuffer{input}};
  return ReadArchive(storage, archive, [] { return true; });
}

bool SerializeUserStorage(ae::RamDomainStorage const& storage,
                          std::vector<std::uint8_t>& bytes) {
  std::vector<std::uint8_t> encoded;
  if (!WriteSnapshot(storage, [&](Input data) {
        encoded.insert(encoded.end(), data.begin(), data.end());
        return true;
      })) return false;
  bytes = std::move(encoded);
  return true;
}

bool DeserializeUserStorage(ae::RamDomainStorage& storage,
                            std::vector<std::uint8_t> const& bytes) {
  Input remaining{bytes};
  return ReadSnapshot(storage, [&](Output data) {
    if (data.size() > remaining.size()) return false;
    std::copy_n(remaining.begin(), data.size(), data.begin());
    remaining = remaining.subspan(data.size());
    return true;
  }, [&] { return remaining.empty(); });
}

bool LoadUserStorageFile(ae::RamDomainStorage& storage,
                         std::filesystem::path const& profile) {
  auto const file_ = profile / "user.bin";
  errno = 0;
  std::ifstream input{file_, std::ios::binary};
  if (!input && errno == ENOENT) {
    storage.state.clear();
    storage.object_scopes.clear();
    return true;
  }
  if (!input) return false;
  return ReadSnapshot(storage, [&](Output bytes) {
    return static_cast<bool>(input.read(reinterpret_cast<char*>(bytes.data()),
                                       bytes.size()));
  }, [&] { return input.peek() == std::char_traits<char>::eof() && !input.bad(); });
}

bool SaveUserStorageFile(ae::RamDomainStorage const& storage,
                         std::filesystem::path const& profile) {
  auto const file_ = profile / "user.bin";
  auto const pending = profile / "user.bin.pending";
  std::error_code ec;
  std::filesystem::create_directories(file_.parent_path(), ec);
  if (ec) return false;
  {
    std::ofstream output{pending, std::ios::binary | std::ios::trunc};
    if (!output) return false;
    if (!WriteSnapshot(storage, [&](Input bytes) {
          return static_cast<bool>(output.write(
              reinterpret_cast<char const*>(bytes.data()), bytes.size()));
        })) return false;
    output.flush();
    if (!output) return false;
    output.close();
    if (!output) return false;
  }
  // The existing committed USER remains intact if the process is killed while
  // writing. The temporary file is never read as state on restart.
#ifdef _WIN32
  return MoveFileExW(pending.c_str(), file_.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  std::filesystem::rename(pending, file_, ec);
  return !ec;
#endif
}
}  // namespace apptraverse
