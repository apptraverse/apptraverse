#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <system_error>
#include <type_traits>

#include "aether/obj/domain.h"

#include "apptraverse/versioned_directory_storage.h"

#include "event_state_version_fixture.h"

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

using apptraverse::test::AddValueEvent;
using apptraverse::test::EventVersionFactory;
using apptraverse::test::EventVersionNode;

static_assert(AddValueEvent::kVersion == 0);

static_assert(!std::is_constructible_v<EventVersionNode, ae::ObjProp,
                                       ae::ObjId, std::int64_t>);
static_assert(
    !std::is_constructible_v<AddValueEvent, ae::ObjProp, std::int64_t>);
static_assert(!std::is_constructible_v<EventVersionFactory, ae::ObjProp,
                                       AddValueEvent::ptr>);

}  // namespace

int main(int argc, char** argv) {
  using apptraverse::test::kBaseSnapshotId;
  using apptraverse::test::kEventVersionNodeId;
  using apptraverse::test::kOldEventId;
  using apptraverse::test::kUnusedSnapshotId;

  if (argc < 3) {
    std::cerr << "usage: apptraverse_event_state_version_runtime_v0 "
                 "<writable-root> <base-root>\n";
    return 1;
  }

  std::filesystem::path const writable_root{argv[1]};
  std::filesystem::path const base_root{argv[2]};

  apptraverse::VersionedDirectoryStorage storage{writable_root, {base_root}};

  ae::Domain domain{ae::Now(), storage};

  auto node = EventVersionNode::ptr::Declare(
      ae::CreateWith{domain}.with_id(kEventVersionNodeId));
  node.Load();
  APPTRAVERSE_CHECK(node);
  APPTRAVERSE_CHECK(node->value_for_test() == 100);
  APPTRAVERSE_CHECK(!node->base_snapshot_id_for_test().IsValid());
  APPTRAVERSE_CHECK(node->journal_size_for_test() == 0);
  APPTRAVERSE_CHECK(node->add_value_apply_calls_for_test() == 0);

  APPTRAVERSE_CHECK(node->AddValue(ae::ObjId{kBaseSnapshotId},
                                   ae::ObjId{kOldEventId},
                                   std::int64_t{20}));
  APPTRAVERSE_CHECK(node->value_for_test() == 120);
  APPTRAVERSE_CHECK(node->base_snapshot_id_for_test().id() == kBaseSnapshotId);
  APPTRAVERSE_CHECK(node->journal_size_for_test() == 1);
  APPTRAVERSE_CHECK(node->add_value_apply_calls_for_test() == 1);

  node.Save();

  auto event = AddValueEvent::ptr::Declare(
      ae::CreateWith{domain}.with_id(kOldEventId));
  event.Load();
  APPTRAVERSE_CHECK(event);
  APPTRAVERSE_CHECK(event->legacy_delta_for_test() == 2);
  APPTRAVERSE_CHECK(event->logical_delta() == 20);

  APPTRAVERSE_CHECK(PathExists(writable_root / "100" /
                               std::to_string(AddValueEvent::kClassId) /
                               "0"));
  APPTRAVERSE_CHECK(!PathExists(writable_root / "100" /
                                std::to_string(AddValueEvent::kClassId) /
                                "1"));

  APPTRAVERSE_CHECK(IsDirectory(writable_root / "1"));
  APPTRAVERSE_CHECK(IsDirectory(writable_root / "100"));
  APPTRAVERSE_CHECK(IsDirectory(writable_root / "200"));
  APPTRAVERSE_CHECK(!PathExists(writable_root / "2"));
  APPTRAVERSE_CHECK(!PathExists(writable_root / "3"));
  APPTRAVERSE_CHECK(!PathExists(writable_root / "101"));
  APPTRAVERSE_CHECK(!PathExists(writable_root / "201"));
  (void)kUnusedSnapshotId;

  return 0;
}
