#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include "aether/obj/domain.h"
#include "aether/obj/obj.h"

#include "apptraverse/directory_domain_storage.h"

namespace {

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::cerr << "CHECK failed: " #cond << " (" << __FILE__ << ':'     \
                << __LINE__ << ")\n";                                     \
      std::exit(1);                                                       \
    }                                                                     \
  } while (0)

std::filesystem::path MakeUniqueTempPath(char const* suffix) {
  static std::atomic<std::uint64_t> counter{0};

  std::error_code ec;
  auto const temp = std::filesystem::temp_directory_path(ec);
  CHECK(!ec);
  CHECK(!temp.empty());

  auto const ticks = static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  auto const id = counter.fetch_add(1, std::memory_order_relaxed);

  return temp / (std::string{"apptraverse_dds_"} + std::to_string(ticks) +
                 "_" + std::to_string(id) + "_" + suffix);
}

std::filesystem::path MakeTestRoot(char const* suffix) {
  std::error_code ec;
  auto root = MakeUniqueTempPath(suffix);
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  CHECK(!ec);
  CHECK(std::filesystem::is_directory(root, ec));
  CHECK(!ec);
  return root;
}

void RemovePath(std::filesystem::path const& path) {
  std::error_code ec;
  std::filesystem::remove_all(path, ec);
}

bool PathExists(std::filesystem::path const& path) {
  std::error_code ec;
  auto const exists = std::filesystem::exists(path, ec);
  return !ec && exists;
}

bool IsDirectory(std::filesystem::path const& path) {
  std::error_code ec;
  auto const is_dir = std::filesystem::is_directory(path, ec);
  return !ec && is_dir;
}

bool IsRegularFile(std::filesystem::path const& path) {
  std::error_code ec;
  auto const is_file = std::filesystem::is_regular_file(path, ec);
  return !ec && is_file;
}

void WriteBytes(apptraverse::DirectoryDomainStorage& storage,
                ae::DomainQuery const& query,
                std::vector<std::uint8_t> const& bytes) {
  auto writer = storage.Store(query);
  CHECK(writer);
  writer->write(bytes.data(), bytes.size());
}

std::vector<std::uint8_t> ReadAll(ae::IDomainStorageReader& reader,
                                  std::size_t size) {
  std::vector<std::uint8_t> bytes(size);
  reader.read(bytes.data(), bytes.size());
  return bytes;
}

void TestStoreLoadRoundtrip() {
  auto const root = MakeTestRoot("store_load");
  apptraverse::DirectoryDomainStorage storage{root};

  ae::DomainQuery const query{ae::ObjId{100}, 123456u, 0};
  std::vector<std::uint8_t> const payload{1, 2, 3, 4, 42, 255};

  WriteBytes(storage, query, payload);

  auto const expected_path =
      root / "100" / "123456" / "0";
  CHECK(IsRegularFile(expected_path));

  auto loaded = storage.Load(query);
  CHECK(loaded.result == ae::DomainLoadResult::kLoaded);
  CHECK(loaded.reader);
  CHECK(ReadAll(*loaded.reader, payload.size()) == payload);

  RemovePath(root);
}

void TestMissingLoad() {
  auto const root = MakeTestRoot("missing");
  apptraverse::DirectoryDomainStorage storage{root};

  auto loaded = storage.Load(ae::DomainQuery{ae::ObjId{999}, 1u, 0});
  CHECK(loaded.result == ae::DomainLoadResult::kEmpty);
  CHECK(!loaded.reader);

  RemovePath(root);
}

void TestEnumerate() {
  auto const root = MakeTestRoot("enumerate");
  apptraverse::DirectoryDomainStorage storage{root};

  ae::ObjId const obj_id{77};
  WriteBytes(storage, ae::DomainQuery{obj_id, 30u, 0}, {1});
  WriteBytes(storage, ae::DomainQuery{obj_id, 10u, 0}, {2});
  WriteBytes(storage, ae::DomainQuery{obj_id, 20u, 0}, {3});
  WriteBytes(storage, ae::DomainQuery{obj_id, 10u, 1}, {4});

  auto const object_dir = root / obj_id.ToString();

  std::error_code ec;
  std::filesystem::create_directory(object_dir / "not-a-number", ec);
  CHECK(!ec);

  {
    std::ofstream file{object_dir / "12345"};
    CHECK(file.is_open());
    file << "file-instead-of-dir";
  }

  std::filesystem::create_directory(object_dir / "4294967296", ec);
  CHECK(!ec);

  auto const classes = storage.Enumerate(obj_id);
  CHECK(classes.size() == 3);
  CHECK(classes[0] == 10u);
  CHECK(classes[1] == 20u);
  CHECK(classes[2] == 30u);

  auto const missing = storage.Enumerate(ae::ObjId{123456});
  CHECK(missing.empty());

  RemovePath(root);
}

void TestIndependentRoots() {
  auto const root_a = MakeTestRoot("root_a");
  auto const root_b = MakeTestRoot("root_b");

  apptraverse::DirectoryDomainStorage first{root_a};
  apptraverse::DirectoryDomainStorage second{root_b};

  ae::DomainQuery const query{ae::ObjId{55}, 7u, 0};
  std::vector<std::uint8_t> const data_a{11, 22};
  std::vector<std::uint8_t> const data_b{33, 44, 55};

  WriteBytes(first, query, data_a);
  WriteBytes(second, query, data_b);

  auto loaded_a = first.Load(query);
  auto loaded_b = second.Load(query);
  CHECK(loaded_a.result == ae::DomainLoadResult::kLoaded);
  CHECK(loaded_b.result == ae::DomainLoadResult::kLoaded);
  CHECK(ReadAll(*loaded_a.reader, data_a.size()) == data_a);
  CHECK(ReadAll(*loaded_b.reader, data_b.size()) == data_b);

  RemovePath(root_a);
  RemovePath(root_b);
}

class BaseObject : public ae::Obj {
  AE_OBJECT(BaseObject, ae::Obj, 0)

 protected:
  BaseObject() = default;

 public:
  explicit BaseObject(ae::ObjProp prop) : ae::Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(base_value))

  std::int32_t base_value{0};
};

class DerivedObject : public BaseObject {
  AE_OBJECT(DerivedObject, BaseObject, 0)

  DerivedObject() = default;

 public:
  explicit DerivedObject(ae::ObjProp prop) : BaseObject{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(derived_value))

  std::int32_t derived_value{0};
};

void TestObjectSystemRoundtrip() {
  auto const root = MakeTestRoot("object_roundtrip");
  constexpr ae::ObjId::Type kObjectId = 100;

  {
    apptraverse::DirectoryDomainStorage storage{root};
    ae::Domain domain{ae::Now(), storage};

    auto object = DerivedObject::ptr::Create(
        ae::CreateWith{domain}.with_id(kObjectId));
    CHECK(object);
    object->base_value = 11;
    object->derived_value = 22;
    object.Save();
  }

  auto const object_dir = root / std::to_string(kObjectId);
  auto const base_path =
      object_dir / std::to_string(BaseObject::kClassId) / "0";
  auto const derived_path =
      object_dir / std::to_string(DerivedObject::kClassId) / "0";
  CHECK(IsRegularFile(base_path));
  CHECK(IsRegularFile(derived_path));

  {
    apptraverse::DirectoryDomainStorage storage{root};
    ae::Domain domain{ae::Now(), storage};

    auto object = DerivedObject::ptr::Declare(
        ae::CreateWith{domain}.with_id(kObjectId));
    object.Load();
    CHECK(object);
    CHECK(object->base_value == 11);
    CHECK(object->derived_value == 22);
  }

  RemovePath(root);
}

void TestRemove() {
  auto const root = MakeTestRoot("remove");
  apptraverse::DirectoryDomainStorage storage{root};

  ae::DomainQuery const query{ae::ObjId{42}, 9u, 0};
  WriteBytes(storage, query, {7, 8, 9});

  auto const object_dir = root / "42";
  CHECK(IsDirectory(object_dir));

  storage.Remove(query.id);
  CHECK(!PathExists(object_dir));

  auto loaded = storage.Load(query);
  CHECK(loaded.result == ae::DomainLoadResult::kEmpty);
  CHECK(!loaded.reader);
  CHECK(!PathExists(object_dir));

  RemovePath(root);
}

void TestCleanUp() {
  auto const root = MakeTestRoot("cleanup");
  apptraverse::DirectoryDomainStorage storage{root};

  WriteBytes(storage, ae::DomainQuery{ae::ObjId{1}, 1u, 0}, {1});
  WriteBytes(storage, ae::DomainQuery{ae::ObjId{2}, 2u, 0}, {2});
  CHECK(IsDirectory(root));

  storage.CleanUp();
  CHECK(!PathExists(root));

  WriteBytes(storage, ae::DomainQuery{ae::ObjId{3}, 3u, 0}, {3, 4});
  auto loaded = storage.Load(ae::DomainQuery{ae::ObjId{3}, 3u, 0});
  CHECK(loaded.result == ae::DomainLoadResult::kLoaded);
  CHECK(loaded.reader);
  CHECK(ReadAll(*loaded.reader, 2) ==
        (std::vector<std::uint8_t>{3, 4}));

  RemovePath(root);
}

void TestFilesystemError() {
  std::error_code ec;
  auto const file_root = MakeUniqueTempPath("file_root");
  std::filesystem::remove_all(file_root, ec);
  {
    std::ofstream file{file_root};
    CHECK(file.is_open());
    file << "not-a-directory";
  }
  CHECK(IsRegularFile(file_root));

  apptraverse::DirectoryDomainStorage storage{file_root};
  ae::DomainQuery const query{ae::ObjId{1}, 1u, 0};

  CHECK(storage.Store(query) == nullptr);

  auto loaded = storage.Load(query);
  CHECK(loaded.result == ae::DomainLoadResult::kEmpty);
  CHECK(!loaded.reader);

  CHECK(storage.Enumerate(query.id).empty());

  RemovePath(file_root);
}

}  // namespace

int main() {
  TestStoreLoadRoundtrip();
  TestMissingLoad();
  TestEnumerate();
  TestIndependentRoots();
  TestObjectSystemRoundtrip();
  TestRemove();
  TestCleanUp();
  TestFilesystemError();
  return 0;
}
