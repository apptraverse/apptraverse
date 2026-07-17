#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <type_traits>

#include "aether/obj/domain.h"

#include "apptraverse/versioned_directory_storage.h"

#include "event_state_version_fixture.h"

namespace {

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
  using apptraverse::test::kNewEventId;
  using apptraverse::test::kOldEventId;

  if (argc < 3) {
    std::cerr << "usage: apptraverse_event_state_version_rollback_v0 "
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
  APPTRAVERSE_CHECK(node->value_for_test() == 150);
  APPTRAVERSE_CHECK(node->base_snapshot_id_for_test().id() == kBaseSnapshotId);
  APPTRAVERSE_CHECK(node->journal_size_for_test() == 2);
  APPTRAVERSE_CHECK(node->add_value_apply_calls_for_test() == 2);

  auto old_event = AddValueEvent::ptr::Declare(
      ae::CreateWith{domain}.with_id(kOldEventId));
  old_event.Load();
  APPTRAVERSE_CHECK(old_event);
  APPTRAVERSE_CHECK(old_event->legacy_delta_for_test() == 2);
  APPTRAVERSE_CHECK(old_event->logical_delta() == 20);

  auto new_event = AddValueEvent::ptr::Declare(
      ae::CreateWith{domain}.with_id(kNewEventId));
  new_event.Load();
  APPTRAVERSE_CHECK(new_event);
  APPTRAVERSE_CHECK(new_event->legacy_delta_for_test() == 3);
  APPTRAVERSE_CHECK(new_event->logical_delta() == 30);

  return 0;
}
