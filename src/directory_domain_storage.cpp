#include "apptraverse/directory_domain_storage.h"

#include <fstream>
#include <ios>
#include <set>
#include <string>

#include "aether/mstream.h"

namespace apptraverse {
namespace {

class DirectoryStorageWriter final : public ae::IDomainStorageWriter {
 public:
  DirectoryStorageWriter(ae::DomainQuery q, std::ofstream&& f)
      : query_{std::move(q)}, file_{std::move(f)} {}

  ~DirectoryStorageWriter() override { file_.close(); }

  void write(void const* data, std::size_t size) override {
    file_.write(reinterpret_cast<std::ofstream::char_type const*>(data),
                static_cast<std::streamsize>(size));
  }

 private:
  ae::DomainQuery query_;
  std::ofstream file_;
};

class DirectoryStorageReader final : public ae::IDomainStorageReader {
 public:
  explicit DirectoryStorageReader(std::ifstream&& f) : file_{std::move(f)} {}
  ~DirectoryStorageReader() override { file_.close(); }

  void read(void* data, std::size_t size) override {
    file_.read(reinterpret_cast<std::ifstream::char_type*>(data),
               static_cast<std::streamsize>(size));
  }

  ae::ReadResult result() const override {
    return ae::ReadResult::kYes;
  }
  void result(ae::ReadResult) override {}

 private:
  std::ifstream file_;
};

}  // namespace

DirectoryDomainStorage::DirectoryDomainStorage(std::filesystem::path root)
    : root_{std::move(root)} {
  std::filesystem::create_directories(root_);
}

DirectoryDomainStorage::~DirectoryDomainStorage() = default;

std::unique_ptr<ae::IDomainStorageWriter> DirectoryDomainStorage::Store(
    ae::DomainQuery const& query) {
  auto class_dir =
      root_ / query.id.ToString() / std::to_string(query.class_id);
  std::filesystem::create_directories(class_dir);
  auto version_data_path = class_dir / std::to_string(query.version);
  std::ofstream f(version_data_path,
                  std::ios::out | std::ios::binary | std::ios::trunc);
  return std::make_unique<DirectoryStorageWriter>(query, std::move(f));
}

ae::ClassList DirectoryDomainStorage::Enumerate(ae::ObjId const& obj_id) {
  std::set<uint32_t> classes;
  auto obj_dir = root_ / obj_id.ToString();
  auto ec = std::error_code{};
  for (auto const& class_dir :
       std::filesystem::directory_iterator(obj_dir, ec)) {
    auto file_name = class_dir.path().filename().string();
    classes.insert(static_cast<std::uint32_t>(std::stoul(file_name)));
  }
  return ae::ClassList{classes.begin(), classes.end()};
}

ae::DomainLoad DirectoryDomainStorage::Load(ae::DomainQuery const& query) {
  auto object_dir = root_ / query.id.ToString();
  auto ec = std::error_code{};
  if (!std::filesystem::exists(object_dir, ec)) {
    return {ae::DomainLoadResult::kEmpty, {}};
  }

  auto is_dir_empty = [&]() {
    auto iter = std::filesystem::directory_iterator{object_dir};
    return std::filesystem::begin(iter) == std::filesystem::end(iter);
  };
  if (is_dir_empty()) {
    return {ae::DomainLoadResult::kRemoved, {}};
  }

  auto class_dir = object_dir / std::to_string(query.class_id);
  auto version_data_path = class_dir / std::to_string(query.version);
  std::ifstream f(version_data_path, std::ios::in | std::ios::binary);
  if (!f.good()) {
    return ae::DomainLoad{ae::DomainLoadResult::kEmpty, {}};
  }

  return {ae::DomainLoadResult::kLoaded,
          std::make_unique<DirectoryStorageReader>(std::move(f))};
}

void DirectoryDomainStorage::Remove(ae::ObjId const& obj_id) {
  auto object_dir = root_ / obj_id.ToString();
  auto ec = std::error_code{};
  if (!std::filesystem::exists(object_dir, ec)) {
    std::filesystem::create_directory(object_dir);
    return;
  }
  if (ec) {
    return;
  }

  for (auto const& class_dir :
       std::filesystem::directory_iterator(object_dir, ec)) {
    auto ec2 = std::error_code{};
    std::filesystem::remove_all(class_dir.path(), ec2);
  }
}

void DirectoryDomainStorage::CleanUp() {
  std::filesystem::remove_all(root_);
  std::filesystem::create_directories(root_);
}

}  // namespace apptraverse
