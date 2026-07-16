#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <system_error>

#include "aether/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/node.h"

#include "node_state_version_fixture.h"

namespace {

bool IsDirectory(std::filesystem::path const& path) {
  std::error_code ec;
  auto const is_dir = std::filesystem::is_directory(path, ec);
  return !ec && is_dir;
}

bool PathExists(std::filesystem::path const& path) {
  std::error_code ec;
  auto const exists = std::filesystem::exists(path, ec);
  return !ec && exists;
}

}  // namespace

int main(int argc, char** argv) {
  using apptraverse::test::kSetBaseValueEventPrototypeId;
  using apptraverse::test::kVersionedFactoryId;
  using apptraverse::test::kVersionedNode3Id;
  using apptraverse::test::SetBaseValueEvent;
  using apptraverse::test::VersionedFactory;
  using apptraverse::test::VersionedNode2;
  using apptraverse::test::VersionedNode3;

  static_assert(VersionedNode2::kVersion == 0);
  static_assert(VersionedNode3::kVersion == 0);

  if (argc < 2) {
    std::cerr
        << "usage: apptraverse_node_state_version_distiller_v0 <base-root>\n";
    return 1;
  }

  std::filesystem::path const base_root{argv[1]};

  apptraverse::DirectoryDomainStorage storage{base_root};
  ae::Domain domain{ae::Now(), storage};

  auto event_prototype = SetBaseValueEvent::ptr::Create(
      ae::CreateWith{domain}.with_id(kSetBaseValueEventPrototypeId),
      std::int64_t{0});
  APPTRAVERSE_CHECK(event_prototype);

  auto factory = VersionedFactory::ptr::Create(
      ae::CreateWith{domain}.with_id(kVersionedFactoryId), event_prototype);
  APPTRAVERSE_CHECK(factory);

  auto node = VersionedNode3::ptr::Create(
      ae::CreateWith{domain}.with_id(kVersionedNode3Id),
      ae::ObjId{kVersionedFactoryId}, std::int32_t{7}, std::int32_t{3});
  APPTRAVERSE_CHECK(node);
  APPTRAVERSE_CHECK(node->legacy_base_value_for_test() == 7);
  APPTRAVERSE_CHECK(node->logical_base_value_for_test() == 70);
  APPTRAVERSE_CHECK(node->legacy_derived_value_for_test() == 3);
  APPTRAVERSE_CHECK(node->logical_derived_value_for_test() == 300);

  factory.Save();
  node.Save();

  APPTRAVERSE_CHECK(IsDirectory(base_root / "1"));
  APPTRAVERSE_CHECK(IsDirectory(base_root / "2"));
  APPTRAVERSE_CHECK(IsDirectory(base_root / "3"));

  APPTRAVERSE_CHECK(PathExists(base_root / "1" /
                               std::to_string(VersionedNode2::kClassId) /
                               "0"));
  APPTRAVERSE_CHECK(PathExists(base_root / "1" /
                               std::to_string(VersionedNode3::kClassId) /
                               "0"));
  APPTRAVERSE_CHECK(PathExists(
      base_root / "1" / std::to_string(apptraverse::Node::kClassId) / "0"));
  APPTRAVERSE_CHECK(!PathExists(base_root / "1" /
                                std::to_string(VersionedNode2::kClassId) /
                                "1"));
  APPTRAVERSE_CHECK(!PathExists(base_root / "1" /
                                std::to_string(VersionedNode3::kClassId) /
                                "1"));

  return 0;
}
