#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <system_error>
#include <type_traits>

#include "aether/obj/domain.h"
#include "aether/obj/version_iterator.h"

#include "apptraverse/node.h"
#include "apptraverse/versioned_directory_storage.h"

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

using apptraverse::test::SetBaseValueEvent;
using apptraverse::test::VersionedFactory;
using apptraverse::test::VersionedNode2;
using apptraverse::test::VersionedNode3;

static_assert(VersionedNode2::kVersion == 1);
static_assert(VersionedNode3::kVersion == 1);
static_assert(ae::HasVersionedLoad<VersionedNode2, 0>::value);
static_assert(ae::HasVersionedLoad<VersionedNode2, 1>::value);
static_assert(ae::HasVersionedSave<VersionedNode2, 0>::value);
static_assert(ae::HasVersionedSave<VersionedNode2, 1>::value);
static_assert(ae::HasVersionedLoad<VersionedNode3, 0>::value);
static_assert(ae::HasVersionedLoad<VersionedNode3, 1>::value);
static_assert(ae::HasVersionedSave<VersionedNode3, 0>::value);
static_assert(ae::HasVersionedSave<VersionedNode3, 1>::value);

static_assert(!std::is_constructible_v<VersionedNode2, ae::ObjProp, ae::ObjId,
                                       std::int32_t>);
static_assert(!std::is_constructible_v<VersionedNode3, ae::ObjProp, ae::ObjId,
                                       std::int32_t, std::int32_t>);
static_assert(
    !std::is_constructible_v<SetBaseValueEvent, ae::ObjProp, std::int64_t>);
static_assert(!std::is_constructible_v<VersionedFactory, ae::ObjProp,
                                       SetBaseValueEvent::ptr>);

}  // namespace

int main(int argc, char** argv) {
  using apptraverse::test::kBaseSnapshotId;
  using apptraverse::test::kSetBaseValueEventId;
  using apptraverse::test::kUnusedSnapshotId;
  using apptraverse::test::kVersionedNode3Id;

  if (argc < 3) {
    std::cerr << "usage: apptraverse_node_state_version_runtime_v1 "
                 "<writable-root> <base-root>\n";
    return 1;
  }

  std::filesystem::path const writable_root{argv[1]};
  std::filesystem::path const base_root{argv[2]};

  apptraverse::VersionedDirectoryStorage storage{writable_root, {base_root}};

  {
    ae::Domain domain{ae::Now(), storage};

    auto node = VersionedNode3::ptr::Declare(
        ae::CreateWith{domain}.with_id(kVersionedNode3Id));
    node.Load();
    APPTRAVERSE_CHECK(node);
    APPTRAVERSE_CHECK(node->base_value_for_test() == 70);
    APPTRAVERSE_CHECK(node->enabled_for_test());
    APPTRAVERSE_CHECK(node->derived_value_for_test() == 300);
    APPTRAVERSE_CHECK(node->generation_for_test() == 1);
    APPTRAVERSE_CHECK(!node->base_snapshot_id_for_test().IsValid());
    APPTRAVERSE_CHECK(node->journal_size_for_test() == 0);
    APPTRAVERSE_CHECK(node->set_base_apply_calls() == 0);

    APPTRAVERSE_CHECK(node->SetBaseValue(ae::ObjId{kBaseSnapshotId},
                                         ae::ObjId{kSetBaseValueEventId},
                                         std::int64_t{90}));
    APPTRAVERSE_CHECK(node->base_value_for_test() == 90);
    APPTRAVERSE_CHECK(node->enabled_for_test());
    APPTRAVERSE_CHECK(node->derived_value_for_test() == 300);
    APPTRAVERSE_CHECK(node->generation_for_test() == 1);
    APPTRAVERSE_CHECK(node->base_snapshot_id_for_test().id() ==
                      kBaseSnapshotId);
    APPTRAVERSE_CHECK(node->journal_size_for_test() == 1);
    APPTRAVERSE_CHECK(node->set_base_apply_calls() == 1);

    APPTRAVERSE_CHECK(PathExists(writable_root / "200" /
                                 std::to_string(VersionedNode2::kClassId) /
                                 "0"));
    APPTRAVERSE_CHECK(PathExists(writable_root / "200" /
                                 std::to_string(VersionedNode2::kClassId) /
                                 "1"));
    APPTRAVERSE_CHECK(PathExists(writable_root / "200" /
                                 std::to_string(VersionedNode3::kClassId) /
                                 "0"));
    APPTRAVERSE_CHECK(PathExists(writable_root / "200" /
                                 std::to_string(VersionedNode3::kClassId) /
                                 "1"));
    APPTRAVERSE_CHECK(PathExists(
        writable_root / "200" / std::to_string(apptraverse::Node::kClassId) /
        "0"));

    node.Save();

    APPTRAVERSE_CHECK(IsDirectory(writable_root / "1"));
    APPTRAVERSE_CHECK(IsDirectory(writable_root / "100"));
    APPTRAVERSE_CHECK(IsDirectory(writable_root / "200"));
    APPTRAVERSE_CHECK(!PathExists(writable_root / "2"));
    APPTRAVERSE_CHECK(!PathExists(writable_root / "3"));
    APPTRAVERSE_CHECK(!PathExists(writable_root / "201"));

    node->CorruptBaseForTest(999, false);
    node->CorruptDerivedForTest(9999, 999);
    APPTRAVERSE_CHECK(node->base_value_for_test() == 999);
    APPTRAVERSE_CHECK(!node->enabled_for_test());
    APPTRAVERSE_CHECK(node->derived_value_for_test() == 9999);
    APPTRAVERSE_CHECK(node->generation_for_test() == 999);
    node.Save();
  }

  {
    ae::Domain domain{ae::Now(), storage};

    auto node = VersionedNode3::ptr::Declare(
        ae::CreateWith{domain}.with_id(kVersionedNode3Id));
    node.Load();
    APPTRAVERSE_CHECK(node);
    APPTRAVERSE_CHECK(node->base_value_for_test() == 90);
    APPTRAVERSE_CHECK(node->enabled_for_test());
    APPTRAVERSE_CHECK(node->derived_value_for_test() == 300);
    APPTRAVERSE_CHECK(node->generation_for_test() == 1);
    APPTRAVERSE_CHECK(node->base_snapshot_id_for_test().id() ==
                      kBaseSnapshotId);
    APPTRAVERSE_CHECK(node->journal_size_for_test() == 1);
    APPTRAVERSE_CHECK(node->set_base_apply_calls() == 1);
    APPTRAVERSE_CHECK(!PathExists(writable_root / "201"));
    (void)kUnusedSnapshotId;
  }

  APPTRAVERSE_CHECK(!PathExists(writable_root / "2"));
  APPTRAVERSE_CHECK(!PathExists(writable_root / "3"));

  return 0;
}
