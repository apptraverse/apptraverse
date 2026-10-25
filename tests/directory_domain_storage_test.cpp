#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include <memory>
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

void SaveNode(DirectoryDomainStorage& storage, Node::ptr& node) {
  node.Save();
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
    SaveNode(storage, node);
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
  SaveNode(storage, node);
  ae::DomainQuery const query = FindSavedProbeLayer(root);
  auto const bytes_after_first = ReadLayerBytes(root, query);
  SaveNode(storage, node);
  CHECK(ReadLayerBytes(root, query) == bytes_after_first);

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
  CHECK(std::filesystem::exists(DirectoryDomainStorageLayerPath(root, query)));
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

  {
    auto writer = storage.Store(query);
    WriteRawLayer(*writer, {});
  }
  CHECK(ReadLayerBytes(root, query) == EmptySerializedLayerBytes());

  std::filesystem::remove_all(root);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestDirectoryDomainStorageRoundtrip();
  apptraverse::test::TestUnchangedNonEmptyLayerSkipped();
  apptraverse::test::TestNewEmptyLayerWritten();
  apptraverse::test::TestReplaceNonEmptyWithEmpty();
  std::cout << "directory_domain_storage_test OK\n";
  return 0;
}
