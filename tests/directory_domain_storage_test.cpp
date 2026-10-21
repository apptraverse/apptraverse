#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/node.h"
#include "apptraverse/object_macros.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

constexpr ae::ObjId::Type kProbeNodeId = 100000;

std::vector<std::uint8_t> EmptySerializedLayerBytes() {
  return {0, 0, 0, 0};
}

std::vector<std::uint8_t> SerializedLayerBytes(
    std::vector<std::uint8_t> const& payload) {
  std::uint32_t const size = static_cast<std::uint32_t>(payload.size());
  std::vector<std::uint8_t> out(sizeof(size));
  std::memcpy(out.data(), &size, sizeof(size));
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

bool SeriOk(ae::seri::SeriResult const& result) {
  return static_cast<bool>(result);
}

void WriteRawLayer(ae::IDomainStorageWriter& writer,
                   std::vector<std::uint8_t> const& payload) {
  std::uint32_t const size = static_cast<std::uint32_t>(payload.size());
  CHECK(SeriOk(writer.Write(ae::seri::SizeWriteTag{size})));
  if (!payload.empty()) {
    CHECK(SeriOk(writer.Write(
        ae::seri::DataWriteTag{payload.data(), payload.size()})));
  }
}

ae::DomainQuery FindSavedProbeLayer(std::filesystem::path const& root) {
  auto const obj_dir = root / std::to_string(kProbeNodeId);
  for (auto const& class_dir : std::filesystem::directory_iterator(obj_dir)) {
    for (auto const& version_file :
         std::filesystem::directory_iterator(class_dir.path())) {
      return ae::DomainQuery{
          ae::ObjId{kProbeNodeId},
          static_cast<std::uint32_t>(
              std::stoul(class_dir.path().filename().string())),
          static_cast<std::uint8_t>(
              std::stoul(version_file.path().filename().string()))};
    }
  }
  CHECK(false);
  return ae::DomainQuery{};
}

DirectoryDomainStorageStoreStats SaveNode(DirectoryDomainStorage& storage,
                                          Node::ptr& node) {
  storage.BeginSaveSession();
  node.Save();
  return storage.SnapshotSaveSessionStats();
}

std::vector<std::uint8_t> ReadLayerBytes(std::filesystem::path const& root,
                                         ae::DomainQuery const& query) {
  auto const path = DirectoryDomainStorageLayerPath(root, query);
  std::ifstream in{path, std::ios::binary | std::ios::ate};
  if (!in) {
    return {};
  }
  auto const size = static_cast<std::size_t>(in.tellg());
  in.seekg(0);
  std::vector<std::uint8_t> bytes(size);
  if (size != 0) {
    in.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(size));
  }
  return bytes;
}

void TestDirectoryDomainStorageRoundtrip() {
  auto root = std::filesystem::temp_directory_path() /
              "apptraverse_directory_domain_storage_test";
  std::filesystem::remove_all(root);

  {
    DirectoryDomainStorage storage{root};
    ae::Domain domain{storage};
    auto node =
        Node::ptr::Create(ae::CreateWith{domain}.with_id(kProbeNodeId));
    CHECK(node.is_valid());
    auto const stats = SaveNode(storage, node);
    CHECK(stats.layer_store_calls >= 1);
    CHECK(stats.layers_written >= 1);
    CHECK(stats.layers_bytes_accepted > 0);
    CHECK(node.domain() == &domain);
  }

  {
    DirectoryDomainStorage storage{root};
    ae::Domain domain{storage};
    auto node =
        Node::ptr::Declare(ae::CreateWith{domain}.with_id(kProbeNodeId));
    node.Load();
    CHECK(node.is_loaded());
    CHECK(node.domain() == &domain);
  }

  std::filesystem::remove_all(root);
}

void TestUnchangedNonEmptyLayerSkipped() {
  auto root = std::filesystem::temp_directory_path() /
              "apptraverse_directory_domain_storage_unchanged";
  std::filesystem::remove_all(root);

  DirectoryDomainStorage storage{root};
  ae::Domain domain{storage};
  auto node = Node::ptr::Create(ae::CreateWith{domain}.with_id(kProbeNodeId));
  auto first = SaveNode(storage, node);
  CHECK(first.layers_written >= 1);
  auto second = SaveNode(storage, node);
  CHECK(second.layers_skipped_unchanged >= 1);
  CHECK(second.layers_written == 0);
  CHECK(second.layers_failed_commits == 0);
  CHECK(!storage.SaveSessionHadFailure());

  std::filesystem::remove_all(root);
}

void TestNewEmptyLayerWritten() {
  auto root = std::filesystem::temp_directory_path() /
              "apptraverse_directory_domain_storage_empty_new";
  std::filesystem::remove_all(root);

  DirectoryDomainStorage storage{root};
  ae::Domain domain{storage};
  ae::DomainQuery const query =
      ae::DomainQuery{ae::ObjId{kProbeNodeId}, 0, 0};
  {
    auto writer = storage.Store(query);
    CHECK(writer != nullptr);
    WriteRawLayer(*writer, {});
  }
  auto const stats = storage.SnapshotSaveSessionStats();
  CHECK(DirectoryDomainStorageLayerFileExists(root, query));
  CHECK(ReadLayerBytes(root, query) == EmptySerializedLayerBytes());

  std::filesystem::remove_all(root);
}

void TestReplaceNonEmptyWithEmpty() {
  auto root = std::filesystem::temp_directory_path() /
              "apptraverse_directory_domain_storage_empty_replace";
  std::filesystem::remove_all(root);

  DirectoryDomainStorage storage{root};
  ae::Domain domain{storage};
  auto node = Node::ptr::Create(ae::CreateWith{domain}.with_id(kProbeNodeId));
  SaveNode(storage, node);
  ae::DomainQuery const query = FindSavedProbeLayer(root);
  std::vector<std::uint8_t> const payload{0x01, 0x02, 0x03};
  {
    auto writer = storage.Store(query);
    WriteRawLayer(*writer, payload);
  }
  CHECK(ReadLayerBytes(root, query) == SerializedLayerBytes(payload));

  storage.BeginSaveSession();
  {
    auto writer = storage.Store(query);
    WriteRawLayer(*writer, {});
  }
  auto const stats = storage.SnapshotSaveSessionStats();
  CHECK(stats.layers_written == 1);
  CHECK(ReadLayerBytes(root, query) == EmptySerializedLayerBytes());
  CHECK(!storage.SaveSessionHadFailure());

  std::filesystem::remove_all(root);
}

void TestWriteFailurePreservesPreviousLayer() {
  auto root = std::filesystem::temp_directory_path() /
              "apptraverse_directory_domain_storage_write_fail";
  std::filesystem::remove_all(root);

  DirectoryDomainStorage storage{root};
  ae::Domain domain{storage};
  auto node = Node::ptr::Create(ae::CreateWith{domain}.with_id(kProbeNodeId));
  SaveNode(storage, node);
  ae::DomainQuery const query = FindSavedProbeLayer(root);
  std::vector<std::uint8_t> const first{0x10, 0x20};
  {
    auto writer = storage.Store(query);
    WriteRawLayer(*writer, first);
  }
  auto const path = DirectoryDomainStorageLayerPath(root, query);
  // A directory cannot be opened as the temporary output file, including when
  // the test runs as root or the platform permits replacing read-only files.
  std::filesystem::path const temporary_path{path.string() + ".tmp"};
  CHECK(std::filesystem::create_directory(temporary_path));

  storage.BeginSaveSession();
  {
    auto writer = storage.Store(query);
    WriteRawLayer(*writer, std::vector<std::uint8_t>{0x99});
  }
  auto const stats = storage.SnapshotSaveSessionStats();
  CHECK(stats.layers_failed_commits >= 1);
  CHECK(stats.layers_written == 0);
  CHECK(storage.SaveSessionHadFailure());
  CHECK(ReadLayerBytes(root, query) == SerializedLayerBytes(first));
  CHECK(std::filesystem::is_directory(temporary_path));

  std::filesystem::remove_all(root);
}

void TestCompareReadErrorFailsSave() {
  auto root = std::filesystem::temp_directory_path() /
              "apptraverse_directory_domain_storage_read_fail";
  std::filesystem::remove_all(root);

  DirectoryDomainStorage storage{root};
  ae::Domain domain{storage};
  auto node = Node::ptr::Create(ae::CreateWith{domain}.with_id(kProbeNodeId));
  SaveNode(storage, node);
  ae::DomainQuery const query = FindSavedProbeLayer(root);
  auto const path = DirectoryDomainStorageLayerPath(root, query);
  // Make the existing layer unreadable as a regular file without relying on
  // permissions. Its marker must survive the failed compare-before-write.
  CHECK(std::filesystem::remove(path));
  CHECK(std::filesystem::create_directory(path));
  auto const marker = path / "marker";
  std::string const marker_bytes{"seed"};
  {
    std::ofstream out{marker, std::ios::binary};
    CHECK(out);
    out << marker_bytes;
    CHECK(out);
  }

  storage.BeginSaveSession();
  {
    auto writer = storage.Store(query);
    WriteRawLayer(*writer, std::vector<std::uint8_t>{0xAB});
  }
  CHECK(storage.SaveSessionHadFailure());
  auto const stats = storage.SnapshotSaveSessionStats();
  CHECK(stats.layers_failed_commits >= 1);
  CHECK(stats.layers_written == 0);
  CHECK(std::filesystem::is_directory(path));
  CHECK(std::filesystem::is_regular_file(marker));
  {
    std::ifstream in{marker, std::ios::binary};
    CHECK(in);
    std::string const actual{std::istreambuf_iterator<char>{in},
                             std::istreambuf_iterator<char>{}};
    CHECK(actual == marker_bytes);
  }
  for (auto const& entry : std::filesystem::directory_iterator{path}) {
    CHECK(entry.path() == marker);
  }
  CHECK(!std::filesystem::exists(path.string() + ".tmp"));

  std::filesystem::remove_all(root);
}

void TestNonDirectoryObjectPathFailsSave() {
  auto root = std::filesystem::temp_directory_path() /
              "apptraverse_directory_domain_storage_finalize_fail";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  DirectoryDomainStorage storage{root};
  constexpr ae::ObjId::Type kBlockedObjId = 999999;
  ae::DomainQuery const query{ae::ObjId{kBlockedObjId}, 1, 0};
  auto const block = root / std::to_string(kBlockedObjId);
  {
    std::ofstream out{block, std::ios::binary};
    out << "not_a_directory";
  }

  storage.BeginSaveSession();
  {
    auto writer = storage.Store(query);
    WriteRawLayer(*writer, std::vector<std::uint8_t>{0xCD});
  }
  CHECK(storage.SaveSessionHadFailure());

  std::filesystem::remove_all(root);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestDirectoryDomainStorageRoundtrip();
  apptraverse::test::TestUnchangedNonEmptyLayerSkipped();
  apptraverse::test::TestNewEmptyLayerWritten();
  apptraverse::test::TestReplaceNonEmptyWithEmpty();
  apptraverse::test::TestWriteFailurePreservesPreviousLayer();
  apptraverse::test::TestCompareReadErrorFailsSave();
  apptraverse::test::TestNonDirectoryObjectPathFailsSave();
  std::cout << "directory_domain_storage_test OK\n";
  return 0;
}
