#include "apptraverse/directory_domain_storage.h"

#include <charconv>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "aether/mstream.h"

namespace apptraverse {
namespace {

class FileStorageWriter final : public ae::IDomainStorageWriter {
 public:
  explicit FileStorageWriter(std::ofstream&& file) : file_{std::move(file)} {}

  ~FileStorageWriter() override {
    if (file_.is_open()) {
      file_.close();
    }
  }

  void write(void const* data, std::size_t size) override {
    file_.write(reinterpret_cast<char const*>(data),
                static_cast<std::streamsize>(size));
  }

 private:
  std::ofstream file_;
};

class FileStorageReader final : public ae::IDomainStorageReader {
 public:
  explicit FileStorageReader(std::ifstream&& file) : file_{std::move(file)} {}

  ~FileStorageReader() override {
    if (file_.is_open()) {
      file_.close();
    }
  }

  void read(void* data, std::size_t size) override {
    file_.read(reinterpret_cast<char*>(data),
               static_cast<std::streamsize>(size));
  }

  ae::ReadResult result() const override { return ae::ReadResult::kYes; }
  void result(ae::ReadResult) override {}

 private:
  std::ifstream file_;
};

bool ParseUint32(std::string_view text, std::uint32_t& out) {
  if (text.empty()) {
    return false;
  }

  std::uint32_t value = 0;
  auto const* const first = text.data();
  auto const* const last = text.data() + text.size();
  auto const [ptr, ec] = std::from_chars(first, last, value);
  if (ec != std::errc{} || ptr != last) {
    return false;
  }

  out = value;
  return true;
}

}  // namespace

DirectoryDomainStorage::DirectoryDomainStorage(std::filesystem::path root)
    : root_{std::move(root)} {}

DirectoryDomainStorage::~DirectoryDomainStorage() = default;

std::filesystem::path DirectoryDomainStorage::ObjectDir(
    ae::ObjId const& obj_id) const {
  return root_ / obj_id.ToString();
}

std::filesystem::path DirectoryDomainStorage::ClassDir(
    ae::DomainQuery const& query) const {
  return ObjectDir(query.id) / std::to_string(query.class_id);
}

std::filesystem::path DirectoryDomainStorage::VersionPath(
    ae::DomainQuery const& query) const {
  return ClassDir(query) / std::to_string(query.version);
}

std::unique_ptr<ae::IDomainStorageWriter> DirectoryDomainStorage::Store(
    ae::DomainQuery const& query) {
  auto const class_dir = ClassDir(query);

  std::error_code ec;
  std::filesystem::create_directories(class_dir, ec);
  if (ec) {
    return nullptr;
  }

  auto const version_path = VersionPath(query);
  std::ofstream file{version_path,
                     std::ios::out | std::ios::binary | std::ios::trunc};
  if (!file.is_open()) {
    return nullptr;
  }

  return std::make_unique<FileStorageWriter>(std::move(file));
}

ae::ClassList DirectoryDomainStorage::Enumerate(ae::ObjId const& obj_id) {
  std::set<std::uint32_t> classes;

  auto const object_dir = ObjectDir(obj_id);
  std::error_code ec;
  for (auto const& entry :
       std::filesystem::directory_iterator{object_dir, ec}) {
    if (ec) {
      break;
    }

    std::error_code entry_ec;
    if (!entry.is_directory(entry_ec) || entry_ec) {
      continue;
    }

    auto const name = entry.path().filename().string();
    std::uint32_t class_id = 0;
    if (!ParseUint32(name, class_id)) {
      continue;
    }

    classes.insert(class_id);
  }

  return ae::ClassList{classes.begin(), classes.end()};
}

ae::DomainLoad DirectoryDomainStorage::Load(ae::DomainQuery const& query) {
  auto const version_path = VersionPath(query);

  std::error_code ec;
  if (!std::filesystem::is_regular_file(version_path, ec) || ec) {
    return {ae::DomainLoadResult::kEmpty, {}};
  }

  std::ifstream file{version_path, std::ios::in | std::ios::binary};
  if (!file.is_open()) {
    return {ae::DomainLoadResult::kEmpty, {}};
  }

  return {ae::DomainLoadResult::kLoaded,
          std::make_unique<FileStorageReader>(std::move(file))};
}

void DirectoryDomainStorage::Remove(ae::ObjId const& obj_id) {
  std::error_code ec;
  std::filesystem::remove_all(ObjectDir(obj_id), ec);
}

void DirectoryDomainStorage::CleanUp() {
  std::error_code ec;
  std::filesystem::remove_all(root_, ec);
}

}  // namespace apptraverse
