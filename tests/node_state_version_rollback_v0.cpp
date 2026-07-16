#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <system_error>
#include <type_traits>

#include "aether/obj/domain.h"

#include "apptraverse/versioned_directory_storage.h"

#include "node_state_version_fixture.h"

namespace {

using apptraverse::test::SetBaseValueEvent;
using apptraverse::test::VersionedFactory;
using apptraverse::test::VersionedNode2;
using apptraverse::test::VersionedNode3;

static_assert(VersionedNode2::kVersion == 0);
static_assert(VersionedNode3::kVersion == 0);

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
  using apptraverse::test::kVersionedNode3Id;

  if (argc < 3) {
    std::cerr << "usage: apptraverse_node_state_version_rollback_v0 "
                 "<writable-root> <base-root>\n";
    return 1;
  }

  std::filesystem::path const writable_root{argv[1]};
  std::filesystem::path const base_root{argv[2]};

  apptraverse::VersionedDirectoryStorage storage{writable_root, {base_root}};

  ae::Domain domain{ae::Now(), storage};

  auto node = VersionedNode3::ptr::Declare(
      ae::CreateWith{domain}.with_id(kVersionedNode3Id));
  node.Load();
  APPTRAVERSE_CHECK(node);
  APPTRAVERSE_CHECK(node->legacy_base_value_for_test() == 9);
  APPTRAVERSE_CHECK(node->logical_base_value_for_test() == 90);
  APPTRAVERSE_CHECK(node->legacy_derived_value_for_test() == 3);
  APPTRAVERSE_CHECK(node->logical_derived_value_for_test() == 300);
  APPTRAVERSE_CHECK(node->base_snapshot_id_for_test().id() == kBaseSnapshotId);
  APPTRAVERSE_CHECK(node->journal_size_for_test() == 1);
  APPTRAVERSE_CHECK(node->set_base_apply_calls() == 1);

  return 0;
}
