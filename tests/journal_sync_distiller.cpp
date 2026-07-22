#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <system_error>

#include "aether/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"

#include "journal_sync_fixture.h"

namespace {

bool IsDirectory(std::filesystem::path const& path) {
  std::error_code ec;
  auto const is_dir = std::filesystem::is_directory(path, ec);
  return !ec && is_dir;
}

}  // namespace

int main(int argc, char** argv) {
  using apptraverse::test::AppendTokenEvent;
  using apptraverse::test::SyncFactory;
  using apptraverse::test::SyncNode;
  using apptraverse::test::kSyncEventPrototypeId;
  using apptraverse::test::kSyncFactoryId;
  using apptraverse::test::kSyncNodeId;

  if (argc < 2) {
    std::cerr << "usage: apptraverse_journal_sync_distiller <base-root>\n";
    return 1;
  }

  std::filesystem::path const base_root{argv[1]};
  apptraverse::DirectoryDomainStorage storage{base_root};
  ae::Domain domain{ae::Now(), storage};

  auto event_prototype = AppendTokenEvent::ptr::Create(
      ae::CreateWith{domain}.with_id(kSyncEventPrototypeId), std::int32_t{0});
  APPTRAVERSE_CHECK(event_prototype);

  auto factory = SyncFactory::ptr::Create(
      ae::CreateWith{domain}.with_id(kSyncFactoryId), event_prototype);
  APPTRAVERSE_CHECK(factory);

  auto node = SyncNode::ptr::Create(
      ae::CreateWith{domain}.with_id(kSyncNodeId), ae::ObjId{kSyncFactoryId});
  APPTRAVERSE_CHECK(node);
  APPTRAVERSE_CHECK(node->tokens().empty());

  factory.Save();
  node.Save();

  APPTRAVERSE_CHECK(IsDirectory(base_root / "1"));
  APPTRAVERSE_CHECK(IsDirectory(base_root / "2"));
  APPTRAVERSE_CHECK(IsDirectory(base_root / "3"));
  return 0;
}
