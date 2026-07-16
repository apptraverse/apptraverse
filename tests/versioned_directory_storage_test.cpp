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
#include "apptraverse/versioned_directory_storage.h"

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

  return temp / (std::string{"apptraverse_vds_"} + std::to_string(ticks) +
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

void WriteBytes(ae::IDomainStorage& storage, ae::DomainQuery const& query,
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

std::vector<std::uint8_t> LoadBytes(ae::IDomainStorage& storage,
                                    ae::DomainQuery const& query,
                                    std::size_t size) {
  auto loaded = storage.Load(query);
  CHECK(loaded.result == ae::DomainLoadResult::kLoaded);
  CHECK(loaded.reader);
  return ReadAll(*loaded.reader, size);
}

struct Roots {
  std::filesystem::path writable;
  std::filesystem::path fallback1;
  std::filesystem::path fallback2;
  std::filesystem::path base;
};

Roots MakeRoots() {
  auto const parent = MakeTestRoot("chain");
  Roots roots{
      parent / "00000003",
      parent / "00000002",
      parent / "00000001",
      parent / "base",
  };

  std::error_code ec;
  for (auto const* path :
       {&roots.writable, &roots.fallback1, &roots.fallback2, &roots.base}) {
    std::filesystem::create_directories(*path, ec);
    CHECK(!ec);
  }
  return roots;
}

void CleanupRoots(Roots const& roots) {
  RemovePath(roots.writable.parent_path());
}

void TestLoadFromWritable() {
  auto const roots = MakeRoots();
  ae::DomainQuery const query{ae::ObjId{100}, 11u, 0};

  {
    apptraverse::DirectoryDomainStorage writable{roots.writable};
    apptraverse::DirectoryDomainStorage fallback1{roots.fallback1};
    apptraverse::DirectoryDomainStorage fallback2{roots.fallback2};
    WriteBytes(writable, query, {1, 1, 1});
    WriteBytes(fallback1, query, {2, 2, 2});
    WriteBytes(fallback2, query, {3, 3, 3});
  }

  apptraverse::VersionedDirectoryStorage storage{
      roots.writable, {roots.fallback1, roots.fallback2, roots.base}};
  CHECK(LoadBytes(storage, query, 3) ==
        (std::vector<std::uint8_t>{1, 1, 1}));

  CleanupRoots(roots);
}

void TestFallbackToFirstPrevious() {
  auto const roots = MakeRoots();
  ae::DomainQuery const query{ae::ObjId{101}, 12u, 0};

  {
    apptraverse::DirectoryDomainStorage fallback1{roots.fallback1};
    apptraverse::DirectoryDomainStorage fallback2{roots.fallback2};
    WriteBytes(fallback1, query, {4, 4});
    WriteBytes(fallback2, query, {5, 5});
  }

  apptraverse::VersionedDirectoryStorage storage{
      roots.writable, {roots.fallback1, roots.fallback2, roots.base}};
  CHECK(LoadBytes(storage, query, 2) == (std::vector<std::uint8_t>{4, 4}));

  CleanupRoots(roots);
}

void TestFallbackToOldest() {
  auto const roots = MakeRoots();
  ae::DomainQuery const query{ae::ObjId{102}, 13u, 0};

  {
    apptraverse::DirectoryDomainStorage base{roots.base};
    WriteBytes(base, query, {9, 8, 7, 6});
  }

  apptraverse::VersionedDirectoryStorage storage{
      roots.writable, {roots.fallback1, roots.fallback2, roots.base}};
  CHECK(LoadBytes(storage, query, 4) ==
        (std::vector<std::uint8_t>{9, 8, 7, 6}));

  CleanupRoots(roots);
}

void TestMissingObject() {
  auto const roots = MakeRoots();
  apptraverse::VersionedDirectoryStorage storage{
      roots.writable, {roots.fallback1, roots.fallback2, roots.base}};

  ae::ObjId const obj_id{404};
  CHECK(storage.Enumerate(obj_id).empty());

  auto loaded = storage.Load(ae::DomainQuery{obj_id, 1u, 0});
  CHECK(loaded.result == ae::DomainLoadResult::kEmpty);
  CHECK(!loaded.reader);

  CleanupRoots(roots);
}

void TestStoreOnlyWritable() {
  auto const roots = MakeRoots();
  ae::DomainQuery const query{ae::ObjId{103}, 14u, 0};

  {
    apptraverse::DirectoryDomainStorage fallback1{roots.fallback1};
    WriteBytes(fallback1, query, {1});
  }

  apptraverse::VersionedDirectoryStorage storage{
      roots.writable, {roots.fallback1, roots.fallback2, roots.base}};
  WriteBytes(storage, query, {42, 43});

  auto const writable_file =
      roots.writable / query.id.ToString() / "14" / "0";
  auto const fallback_file =
      roots.fallback1 / query.id.ToString() / "14" / "0";
  CHECK(IsRegularFile(writable_file));
  CHECK(IsRegularFile(fallback_file));

  std::error_code ec;
  auto const writable_size = std::filesystem::file_size(writable_file, ec);
  CHECK(!ec);
  CHECK(writable_size == 2);

  auto const fallback_size = std::filesystem::file_size(fallback_file, ec);
  CHECK(!ec);
  CHECK(fallback_size == 1);

  CHECK(LoadBytes(storage, query, 2) ==
        (std::vector<std::uint8_t>{42, 43}));

  CleanupRoots(roots);
}

void TestEnumerateDoesNotUnion() {
  auto const roots = MakeRoots();
  ae::ObjId const obj_id{104};

  {
    apptraverse::DirectoryDomainStorage writable{roots.writable};
    apptraverse::DirectoryDomainStorage fallback1{roots.fallback1};
    WriteBytes(writable, ae::DomainQuery{obj_id, 300u, 0}, {3});
    WriteBytes(fallback1, ae::DomainQuery{obj_id, 100u, 0}, {1});
    WriteBytes(fallback1, ae::DomainQuery{obj_id, 200u, 0}, {2});
  }

  apptraverse::VersionedDirectoryStorage storage{
      roots.writable, {roots.fallback1, roots.fallback2, roots.base}};
  auto const classes = storage.Enumerate(obj_id);
  CHECK(classes.size() == 1);
  CHECK(classes[0] == 300u);

  CleanupRoots(roots);
}

void TestNoClassFileMixing() {
  auto const roots = MakeRoots();
  ae::ObjId const obj_id{105};

  {
    apptraverse::DirectoryDomainStorage writable{roots.writable};
    apptraverse::DirectoryDomainStorage fallback1{roots.fallback1};
    WriteBytes(writable, ae::DomainQuery{obj_id, 300u, 0}, {30});
    WriteBytes(fallback1, ae::DomainQuery{obj_id, 100u, 0}, {10});
    WriteBytes(fallback1, ae::DomainQuery{obj_id, 300u, 0}, {33});
  }

  apptraverse::VersionedDirectoryStorage storage{
      roots.writable, {roots.fallback1, roots.fallback2, roots.base}};

  CHECK(LoadBytes(storage, ae::DomainQuery{obj_id, 300u, 0}, 1) ==
        (std::vector<std::uint8_t>{30}));

  auto loaded = storage.Load(ae::DomainQuery{obj_id, 100u, 0});
  CHECK(loaded.result == ae::DomainLoadResult::kEmpty);
  CHECK(!loaded.reader);

  CleanupRoots(roots);
}

void TestEmptyObjectDirectoryHidesFallback() {
  auto const roots = MakeRoots();
  ae::ObjId const obj_id{106};

  {
    apptraverse::DirectoryDomainStorage fallback1{roots.fallback1};
    WriteBytes(fallback1, ae::DomainQuery{obj_id, 77u, 0}, {7, 7});
  }

  std::error_code ec;
  std::filesystem::create_directories(roots.writable / obj_id.ToString(),
                                      ec);
  CHECK(!ec);
  CHECK(IsDirectory(roots.writable / obj_id.ToString()));

  apptraverse::VersionedDirectoryStorage storage{
      roots.writable, {roots.fallback1, roots.fallback2, roots.base}};

  CHECK(storage.Enumerate(obj_id).empty());
  auto loaded = storage.Load(ae::DomainQuery{obj_id, 77u, 0});
  CHECK(loaded.result == ae::DomainLoadResult::kEmpty);
  CHECK(!loaded.reader);

  CleanupRoots(roots);
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

void TestInheritedObjectRoundtrip() {
  auto const roots = MakeRoots();
  constexpr ae::ObjId::Type kObjectId = 200;

  {
    apptraverse::DirectoryDomainStorage fallback{roots.fallback1};
    ae::Domain domain{ae::Now(), fallback};
    auto object = DerivedObject::ptr::Create(
        ae::CreateWith{domain}.with_id(kObjectId));
    CHECK(object);
    object->base_value = 1;
    object->derived_value = 2;
    object.Save();
  }

  {
    apptraverse::VersionedDirectoryStorage storage{
        roots.writable, {roots.fallback1, roots.fallback2, roots.base}};
    ae::Domain domain{ae::Now(), storage};

    auto object = DerivedObject::ptr::Declare(
        ae::CreateWith{domain}.with_id(kObjectId));
    object.Load();
    CHECK(object);
    CHECK(object->base_value == 1);
    CHECK(object->derived_value == 2);

    object->base_value = 11;
    object->derived_value = 22;
    object.Save();
  }

  auto const object_dir = roots.writable / std::to_string(kObjectId);
  auto const base_path =
      object_dir / std::to_string(BaseObject::kClassId) / "0";
  auto const derived_path =
      object_dir / std::to_string(DerivedObject::kClassId) / "0";
  CHECK(IsRegularFile(base_path));
  CHECK(IsRegularFile(derived_path));

  {
    apptraverse::VersionedDirectoryStorage storage{
        roots.writable, {roots.fallback1, roots.fallback2, roots.base}};
    ae::Domain domain{ae::Now(), storage};

    auto object = DerivedObject::ptr::Declare(
        ae::CreateWith{domain}.with_id(kObjectId));
    object.Load();
    CHECK(object);
    CHECK(object->base_value == 11);
    CHECK(object->derived_value == 22);
  }

  CleanupRoots(roots);
}

void TestRemoveOnlyWritable() {
  auto const roots = MakeRoots();
  ae::DomainQuery const query{ae::ObjId{107}, 15u, 0};

  {
    apptraverse::DirectoryDomainStorage writable{roots.writable};
    apptraverse::DirectoryDomainStorage fallback1{roots.fallback1};
    WriteBytes(writable, query, {8});
    WriteBytes(fallback1, query, {9});
  }

  apptraverse::VersionedDirectoryStorage storage{
      roots.writable, {roots.fallback1, roots.fallback2, roots.base}};
  storage.Remove(query.id);

  CHECK(!PathExists(roots.writable / query.id.ToString()));
  CHECK(IsDirectory(roots.fallback1 / query.id.ToString()));
  CHECK(LoadBytes(storage, query, 1) == (std::vector<std::uint8_t>{9}));
  CHECK(!PathExists(roots.writable / query.id.ToString()));

  CleanupRoots(roots);
}

void TestCleanUpOnlyWritable() {
  auto const roots = MakeRoots();
  ae::DomainQuery const query_a{ae::ObjId{108}, 16u, 0};
  ae::DomainQuery const query_b{ae::ObjId{109}, 17u, 0};

  {
    apptraverse::DirectoryDomainStorage writable{roots.writable};
    apptraverse::DirectoryDomainStorage fallback1{roots.fallback1};
    WriteBytes(writable, query_a, {1});
    WriteBytes(writable, query_b, {2});
    WriteBytes(fallback1, query_a, {3});
  }

  apptraverse::VersionedDirectoryStorage storage{
      roots.writable, {roots.fallback1, roots.fallback2, roots.base}};
  storage.CleanUp();

  CHECK(!PathExists(roots.writable));
  CHECK(IsDirectory(roots.fallback1));
  CHECK(IsDirectory(roots.fallback2));
  CHECK(IsDirectory(roots.base));
  CHECK(LoadBytes(storage, query_a, 1) == (std::vector<std::uint8_t>{3}));

  WriteBytes(storage, query_b, {4, 5});
  CHECK(IsRegularFile(roots.writable / query_b.id.ToString() / "17" / "0"));
  CHECK(LoadBytes(storage, query_b, 2) ==
        (std::vector<std::uint8_t>{4, 5}));

  CleanupRoots(roots);
}

void TestFallbackOrderFromConstructor() {
  auto const parent = MakeTestRoot("order");
  auto const writable_a = parent / "writable_a";
  auto const writable_b = parent / "writable_b";
  auto const first = parent / "first";
  auto const second = parent / "second";

  std::error_code ec;
  for (auto const* path : {&writable_a, &writable_b, &first, &second}) {
    std::filesystem::create_directories(*path, ec);
    CHECK(!ec);
  }

  ae::DomainQuery const query{ae::ObjId{110}, 18u, 0};
  {
    apptraverse::DirectoryDomainStorage storage_first{first};
    apptraverse::DirectoryDomainStorage storage_second{second};
    WriteBytes(storage_first, query, {10});
    WriteBytes(storage_second, query, {20});
  }

  apptraverse::VersionedDirectoryStorage storage_a{writable_a,
                                                   {first, second}};
  apptraverse::VersionedDirectoryStorage storage_b{writable_b,
                                                   {second, first}};

  CHECK(LoadBytes(storage_a, query, 1) == (std::vector<std::uint8_t>{10}));
  CHECK(LoadBytes(storage_b, query, 1) == (std::vector<std::uint8_t>{20}));

  RemovePath(parent);
}

void TestBadWritableRoot() {
  auto const roots = MakeRoots();
  ae::DomainQuery const query{ae::ObjId{111}, 19u, 0};

  {
    apptraverse::DirectoryDomainStorage fallback1{roots.fallback1};
    WriteBytes(fallback1, query, {6, 6});
  }

  std::error_code ec;
  RemovePath(roots.writable);
  {
    std::ofstream file{roots.writable};
    CHECK(file.is_open());
    file << "not-a-directory";
  }
  CHECK(IsRegularFile(roots.writable));

  apptraverse::VersionedDirectoryStorage storage{
      roots.writable, {roots.fallback1, roots.fallback2, roots.base}};

  CHECK(storage.Store(query) == nullptr);
  CHECK(LoadBytes(storage, query, 2) ==
        (std::vector<std::uint8_t>{6, 6}));

  CleanupRoots(roots);
}

}  // namespace

int main() {
  TestLoadFromWritable();
  TestFallbackToFirstPrevious();
  TestFallbackToOldest();
  TestMissingObject();
  TestStoreOnlyWritable();
  TestEnumerateDoesNotUnion();
  TestNoClassFileMixing();
  TestEmptyObjectDirectoryHidesFallback();
  TestInheritedObjectRoundtrip();
  TestRemoveOnlyWritable();
  TestCleanUpOnlyWritable();
  TestFallbackOrderFromConstructor();
  TestBadWritableRoot();
  return 0;
}
