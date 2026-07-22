#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <system_error>
#include <vector>

#include "aether/obj/domain.h"

#include "apptraverse/event_record.h"
#include "apptraverse/versioned_directory_storage.h"

#include "node_journal_fixture.h"

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

bool ReadFileBytes(std::filesystem::path const& path,
                   std::vector<std::uint8_t>& out) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec) || ec) {
    return false;
  }

  auto const size = std::filesystem::file_size(path, ec);
  if (ec) {
    return false;
  }

  std::ifstream file{path, std::ios::in | std::ios::binary};
  if (!file.is_open()) {
    return false;
  }

  out.assign(static_cast<std::size_t>(size), 0);
  if (size > 0) {
    file.read(reinterpret_cast<char*>(out.data()),
              static_cast<std::streamsize>(size));
  }
  return static_cast<bool>(file) || size == 0;
}

}  // namespace

int main(int argc, char** argv) {
  using apptraverse::EventDeliveryState;
  using apptraverse::test::JournalNode;
  using apptraverse::test::kBaseSnapshotId;
  using apptraverse::test::kFirstEventId;
  using apptraverse::test::kJournalFactoryId;
  using apptraverse::test::kJournalNodeId;
  using apptraverse::test::kJournalReplicaId;
  using apptraverse::test::kSecondEventId;
  using apptraverse::test::kUnusedSecondSnapshotId;

  if (argc < 3) {
    std::cerr << "usage: apptraverse_node_journal_runtime "
                 "<writable-root> <base-root>\n";
    return 1;
  }

  std::filesystem::path const writable_root{argv[1]};
  std::filesystem::path const base_root{argv[2]};

  apptraverse::VersionedDirectoryStorage storage{writable_root, {base_root}};

  {
    ae::Domain domain{ae::Now(), storage};

    auto node = JournalNode::ptr::Declare(
        ae::CreateWith{domain}.with_id(kJournalNodeId));
    node.Load();
    APPTRAVERSE_CHECK(node);
    APPTRAVERSE_CHECK(node->value() == 10);
    APPTRAVERSE_CHECK(node->factory_id().id() == kJournalFactoryId);
    APPTRAVERSE_CHECK(!node->base_snapshot_id_for_test().IsValid());
    APPTRAVERSE_CHECK(node->journal_size_for_test() == 0);
    APPTRAVERSE_CHECK(node->apply_calls() == 0);

    node->InitializeReplicaForTest(kJournalReplicaId);

    APPTRAVERSE_CHECK(node->SetValue(ae::ObjId{kBaseSnapshotId},
                                     ae::ObjId{kFirstEventId},
                                     std::int32_t{42}));
    APPTRAVERSE_CHECK(node->value() == 42);
    APPTRAVERSE_CHECK(node->apply_calls() == 1);
    APPTRAVERSE_CHECK(node->base_snapshot_id_for_test().id() == kBaseSnapshotId);
    APPTRAVERSE_CHECK(node->journal_size_for_test() == 1);
    APPTRAVERSE_CHECK(node->journal_identity_at_for_test(0).origin ==
                      kJournalReplicaId);
    APPTRAVERSE_CHECK(node->journal_identity_at_for_test(0).sequence == 1);
    APPTRAVERSE_CHECK(node->journal_delivery_state_at_for_test(0) ==
                      EventDeliveryState::kPending);
    APPTRAVERSE_CHECK(node->journal_event_id_at_for_test(0).id() ==
                      kFirstEventId);

    node.Save();

    APPTRAVERSE_CHECK(IsDirectory(writable_root / "1"));
    APPTRAVERSE_CHECK(IsDirectory(writable_root / "100"));
    APPTRAVERSE_CHECK(IsDirectory(writable_root / "200"));
    APPTRAVERSE_CHECK(!PathExists(writable_root / "2"));
    APPTRAVERSE_CHECK(!PathExists(writable_root / "3"));

    node->CorruptMaterializedValueForTest(999);
    APPTRAVERSE_CHECK(node->value() == 999);
    node.Save();
  }

  std::vector<std::uint8_t> snapshot_bytes_before;
  {
    ae::Domain domain{ae::Now(), storage};

    auto node = JournalNode::ptr::Declare(
        ae::CreateWith{domain}.with_id(kJournalNodeId));
    node.Load();
    APPTRAVERSE_CHECK(node);
    APPTRAVERSE_CHECK(node->value() == 42);
    APPTRAVERSE_CHECK(node->apply_calls() == 1);
    APPTRAVERSE_CHECK(node->replica_id_for_test() == kJournalReplicaId);
    APPTRAVERSE_CHECK(node->base_snapshot_id_for_test().id() == kBaseSnapshotId);
    APPTRAVERSE_CHECK(node->journal_size_for_test() == 1);
    APPTRAVERSE_CHECK(node->journal_identity_at_for_test(0).sequence == 1);
    APPTRAVERSE_CHECK(node->journal_delivery_state_at_for_test(0) ==
                      EventDeliveryState::kPending);
    APPTRAVERSE_CHECK(node->journal_event_id_at_for_test(0).id() ==
                      kFirstEventId);
    APPTRAVERSE_CHECK(node->factory_id().id() == kJournalFactoryId);

    auto const snapshot_layer =
        writable_root / "200" / std::to_string(JournalNode::kClassId) / "0";
    APPTRAVERSE_CHECK(ReadFileBytes(snapshot_layer, snapshot_bytes_before));
    APPTRAVERSE_CHECK(!snapshot_bytes_before.empty());

    APPTRAVERSE_CHECK(node->SetValue(ae::ObjId{kUnusedSecondSnapshotId},
                                     ae::ObjId{kSecondEventId},
                                     std::int32_t{84}));
    APPTRAVERSE_CHECK(node->value() == 84);
    APPTRAVERSE_CHECK(node->apply_calls() == 2);
    APPTRAVERSE_CHECK(node->base_snapshot_id_for_test().id() == kBaseSnapshotId);
    APPTRAVERSE_CHECK(node->journal_size_for_test() == 2);
    APPTRAVERSE_CHECK(node->journal_identity_at_for_test(0).sequence == 1);
    APPTRAVERSE_CHECK(node->journal_identity_at_for_test(1).sequence == 2);
    APPTRAVERSE_CHECK(node->journal_delivery_state_at_for_test(0) ==
                      EventDeliveryState::kPending);
    APPTRAVERSE_CHECK(node->journal_delivery_state_at_for_test(1) ==
                      EventDeliveryState::kPending);
    APPTRAVERSE_CHECK(node->journal_event_id_at_for_test(1).id() ==
                      kSecondEventId);

    node.Save();

    APPTRAVERSE_CHECK(IsDirectory(writable_root / "102"));
    APPTRAVERSE_CHECK(!PathExists(writable_root / "201"));

    std::vector<std::uint8_t> snapshot_bytes_after;
    APPTRAVERSE_CHECK(ReadFileBytes(snapshot_layer, snapshot_bytes_after));
    APPTRAVERSE_CHECK(snapshot_bytes_after == snapshot_bytes_before);
  }

  {
    ae::Domain domain{ae::Now(), storage};

    auto node = JournalNode::ptr::Declare(
        ae::CreateWith{domain}.with_id(kJournalNodeId));
    node.Load();
    APPTRAVERSE_CHECK(node);
    APPTRAVERSE_CHECK(node->value() == 84);
    APPTRAVERSE_CHECK(node->apply_calls() == 2);
    APPTRAVERSE_CHECK(node->replica_id_for_test() == kJournalReplicaId);
    APPTRAVERSE_CHECK(node->base_snapshot_id_for_test().id() == kBaseSnapshotId);
    APPTRAVERSE_CHECK(node->journal_size_for_test() == 2);
    APPTRAVERSE_CHECK(node->journal_identity_at_for_test(0).sequence == 1);
    APPTRAVERSE_CHECK(node->journal_identity_at_for_test(1).sequence == 2);
    APPTRAVERSE_CHECK(node->journal_delivery_state_at_for_test(0) ==
                      EventDeliveryState::kPending);
    APPTRAVERSE_CHECK(node->journal_delivery_state_at_for_test(1) ==
                      EventDeliveryState::kPending);
    APPTRAVERSE_CHECK(node->journal_event_id_at_for_test(0).id() ==
                      kFirstEventId);
    APPTRAVERSE_CHECK(node->journal_event_id_at_for_test(1).id() ==
                      kSecondEventId);
    APPTRAVERSE_CHECK(node->factory_id().id() == kJournalFactoryId);

    auto first_event = node->journal_event_at_for_test(0);
    APPTRAVERSE_CHECK(first_event.is_valid());
    APPTRAVERSE_CHECK(first_event.id().id() == kFirstEventId);
  }

  APPTRAVERSE_CHECK(!PathExists(writable_root / "2"));
  APPTRAVERSE_CHECK(!PathExists(writable_root / "3"));
  APPTRAVERSE_CHECK(!PathExists(writable_root / "201"));
  APPTRAVERSE_CHECK(IsDirectory(writable_root / "100"));
  APPTRAVERSE_CHECK(IsDirectory(writable_root / "102"));

  return 0;
}
