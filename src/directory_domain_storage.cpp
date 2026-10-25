#include "apptraverse/directory_domain_storage.h"

#include <charconv>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "apptraverse/noninteractive_crt.h"

namespace apptraverse {

namespace {

[[noreturn]] void FatalStorageOperation(char const* operation) {
  WriteFatalStderr(operation);
  std::abort();
}

std::vector<std::uint8_t> ReadExistingLayerBytes(
    std::filesystem::path const& path) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    return {};
  }
  std::ifstream in{path, std::ios::binary | std::ios::ate};
  auto const size = static_cast<std::size_t>(in.tellg());
  if (size == 0) {
    return {};
  }
  in.seekg(0);
  std::vector<std::uint8_t> bytes(size);
  in.read(reinterpret_cast<char*>(bytes.data()),
          static_cast<std::streamsize>(size));
  return bytes;
}

void WriteLayerFile(std::filesystem::path const& path,
                    std::vector<std::uint8_t> const& bytes) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    FatalStorageOperation("fatal: create storage layer directory failed\n");
  }
  std::ofstream out{path, std::ios::out | std::ios::binary | std::ios::trunc};
  if (!bytes.empty()) {
    out.write(reinterpret_cast<char const*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  }
}

class CompareOnCloseDirectoryStorageWriter final : public ae::IDomainStorageWriter {
 public:
  CompareOnCloseDirectoryStorageWriter(ae::DomainQuery query,
                                       std::filesystem::path path)
      : query_{std::move(query)}, path_{std::move(path)} {}

  ~CompareOnCloseDirectoryStorageWriter() override { CommitIfNeeded(); }

  ae::seri::SeriResult Write(ae::seri::SizeWriteTag data) override {
    auto const u_size = static_cast<std::uint32_t>(data.size);
    return Write(ae::seri::DataTag{u_size});
  }

  ae::seri::SeriResult Write(ae::seri::DataWriteTag data) override {
    auto const* begin = static_cast<std::uint8_t const*>(data.data);
    buffer_.insert(buffer_.end(), begin, begin + data.size);
    return ae::Ok{ae::seri::good};
  }

 private:
  void CommitIfNeeded() {
    if (committed_) {
      return;
    }
    committed_ = true;
    std::vector<std::uint8_t> const existing = ReadExistingLayerBytes(path_);
    if (!existing.empty() && existing == buffer_) {
      return;
    }
    WriteLayerFile(path_, buffer_);
  }

  ae::DomainQuery query_;
  std::filesystem::path path_;
  std::vector<std::uint8_t> buffer_;
  bool committed_{false};
};

class DirectoryStorageReader final : public ae::IDomainStorageReader {
 public:
  explicit DirectoryStorageReader(std::ifstream&& f) : file_{std::move(f)} {}

  ~DirectoryStorageReader() override { file_.close(); }

  ae::seri::SeriResult Read(ae::seri::SizeReadTag data) override {
    std::uint32_t u_size{};
    TRY_RESULT(Read(ae::seri::DataTag{u_size}));
    data.size = static_cast<std::size_t>(u_size);
    return ae::Ok{ae::seri::good};
  }

  ae::seri::SeriResult Read(ae::seri::DataReadTag data) override {
    file_.read(reinterpret_cast<std::ifstream::char_type*>(data.data),
               static_cast<std::streamsize>(data.size));
    return ae::Ok{ae::seri::good};
  }

 private:
  std::ifstream file_;
};

}  // namespace

std::filesystem::path DirectoryDomainStorageLayerPath(
    std::filesystem::path const& root, ae::DomainQuery const& query) {
  return root / std::to_string(query.id.id()) /
         std::to_string(query.class_id) / std::to_string(query.version);
}

DirectoryDomainStorage::DirectoryDomainStorage(std::filesystem::path root)
    : root_{std::move(root)} {
  std::error_code ec;
  std::filesystem::create_directories(root_, ec);
  if (ec) {
    FatalStorageOperation("fatal: create storage directory failed\n");
  }
}

DirectoryDomainStorage::~DirectoryDomainStorage() = default;

std::unique_ptr<ae::IDomainStorageWriter> DirectoryDomainStorage::Store(
    ae::DomainQuery const& query) {
  auto path = DirectoryDomainStorageLayerPath(root_, query);
  return std::make_unique<CompareOnCloseDirectoryStorageWriter>(std::move(query),
                                                                std::move(path));
}

ae::ClassList DirectoryDomainStorage::Enumerate(ae::ObjId const& obj_id) {
  std::set<std::uint32_t> classes;
  auto const object_dir = root_ / std::to_string(obj_id.id());
  std::error_code ec;
  if (!std::filesystem::exists(object_dir, ec) || ec) {
    return {};
  }
  std::filesystem::directory_iterator it{object_dir, ec}, end;
  if (ec) {
    FatalStorageOperation("fatal: enumerate storage object directory failed\n");
  }
  for (; it != end; it.increment(ec)) {
    if (ec) {
      FatalStorageOperation("fatal: advance storage directory iterator failed\n");
    }
    auto const filename = it->path().filename().string();
    std::uint32_t class_id{};
    auto const parsed = std::from_chars(filename.data(),
                                        filename.data() + filename.size(),
                                        class_id);
    if (parsed.ec == std::errc{} &&
        parsed.ptr == filename.data() + filename.size()) {
      classes.insert(class_id);
    }
  }
  return ae::ClassList{classes.begin(), classes.end()};
}

ae::DomainLoad DirectoryDomainStorage::Load(ae::DomainQuery const& query) {
  auto const object_dir = root_ / std::to_string(query.id.id());
  std::error_code ec;
  if (!std::filesystem::exists(object_dir, ec) || ec) {
    return {ae::DomainLoadResult::kEmpty, {}};
  }
  if (std::filesystem::is_empty(object_dir, ec) || ec) {
    return {ae::DomainLoadResult::kRemoved, {}};
  }
  auto const path = DirectoryDomainStorageLayerPath(root_, query);
  std::ifstream file{path, std::ios::binary};
  if (!file.is_open()) {
    return {ae::DomainLoadResult::kEmpty, {}};
  }
  return {ae::DomainLoadResult::kLoaded,
          std::make_unique<DirectoryStorageReader>(std::move(file))};
}

void DirectoryDomainStorage::Remove(ae::ObjId const& obj_id) {
  auto const object_dir = root_ / std::to_string(obj_id.id());
  std::error_code ec;
  std::filesystem::remove_all(object_dir, ec);
  if (ec) {
    FatalStorageOperation("fatal: remove stored object failed\n");
  }
  std::filesystem::create_directories(object_dir, ec);
  if (ec) {
    FatalStorageOperation("fatal: create stored object tombstone failed\n");
  }
}

void DirectoryDomainStorage::CleanUp() {
  std::error_code ec;
  std::filesystem::remove_all(root_, ec);
  if (ec) {
    FatalStorageOperation("fatal: clear storage failed\n");
  }
  std::filesystem::create_directories(root_, ec);
  if (ec) {
    FatalStorageOperation("fatal: recreate storage directory failed\n");
  }
}

}  // namespace apptraverse
