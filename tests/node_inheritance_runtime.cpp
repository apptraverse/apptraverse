#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <system_error>
#include <type_traits>
#include <vector>

#include "aether/obj/domain.h"

#include "apptraverse/node.h"
#include "apptraverse/versioned_directory_storage.h"

#include "node_inheritance_fixture.h"

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

using apptraverse::test::Node1;
using apptraverse::test::Node2;
using apptraverse::test::Node2Factory;
using apptraverse::test::Node3;
using apptraverse::test::Node3Factory;
using apptraverse::test::SetObjectEvent;
using apptraverse::test::SetValue3Event;

static_assert(!std::is_constructible_v<Node1, ae::ObjProp, std::int32_t>);
static_assert(!std::is_constructible_v<Node2, ae::ObjProp, ae::ObjId,
                                       Node1::ptr>);
static_assert(!std::is_constructible_v<Node3, ae::ObjProp, ae::ObjId,
                                       Node1::ptr, ae::ObjId, std::int32_t>);
static_assert(
    !std::is_constructible_v<SetObjectEvent, ae::ObjProp, ae::ObjId>);
static_assert(
    !std::is_constructible_v<SetValue3Event, ae::ObjProp, std::int32_t>);
static_assert(!std::is_constructible_v<Node2Factory, ae::ObjProp,
                                       SetObjectEvent::ptr>);
static_assert(!std::is_constructible_v<Node3Factory, ae::ObjProp,
                                       SetValue3Event::ptr>);

}  // namespace

int main(int argc, char** argv) {
  using apptraverse::test::kBaseSnapshotId;
  using apptraverse::test::kNode1AId;
  using apptraverse::test::kNode1BId;
  using apptraverse::test::kNode3Id;
  using apptraverse::test::kSetObjectEventId;
  using apptraverse::test::kSetValue3EventId;
  using apptraverse::test::kUnusedSecondSnapshotId;

  if (argc < 3) {
    std::cerr << "usage: apptraverse_node_inheritance_runtime "
                 "<writable-root> <base-root>\n";
    return 1;
  }

  std::filesystem::path const writable_root{argv[1]};
  std::filesystem::path const base_root{argv[2]};

  apptraverse::VersionedDirectoryStorage storage{writable_root, {base_root}};

  std::vector<std::uint8_t> node3_layer_before;
  std::vector<std::uint8_t> node2_layer_before;
  std::vector<std::uint8_t> node_layer_before;

  {
    ae::Domain domain{ae::Now(), storage};

    auto node3 = Node3::ptr::Declare(
        ae::CreateWith{domain}.with_id(kNode3Id));
    node3.Load();
    APPTRAVERSE_CHECK(node3);
    APPTRAVERSE_CHECK(node3->object_id().id() == kNode1AId);
    APPTRAVERSE_CHECK(node3->object());
    APPTRAVERSE_CHECK(node3->object()->marker() == 111);
    APPTRAVERSE_CHECK(node3->value3() == 30);
    APPTRAVERSE_CHECK(!node3->base_snapshot_id_for_test().IsValid());
    APPTRAVERSE_CHECK(node3->journal_size_for_test() == 0);
    APPTRAVERSE_CHECK(node3->set_object_apply_calls() == 0);
    APPTRAVERSE_CHECK(node3->set_value3_apply_calls() == 0);

    node3->InitializeReplicaForTest(apptraverse::ReplicaId{1});

    APPTRAVERSE_CHECK(node3->SetObject(ae::ObjId{kBaseSnapshotId},
                                       ae::ObjId{kSetObjectEventId},
                                       ae::ObjId{kNode1BId}));
    APPTRAVERSE_CHECK(node3->object_id().id() == kNode1BId);
    APPTRAVERSE_CHECK(node3->object());
    APPTRAVERSE_CHECK(node3->object()->marker() == 222);
    APPTRAVERSE_CHECK(node3->value3() == 30);
    APPTRAVERSE_CHECK(node3->base_snapshot_id_for_test().id() ==
                      kBaseSnapshotId);
    APPTRAVERSE_CHECK(node3->journal_size_for_test() == 1);
    APPTRAVERSE_CHECK(node3->set_object_apply_calls() == 1);
    APPTRAVERSE_CHECK(node3->set_value3_apply_calls() == 0);

    auto const snapshot_node3 =
        writable_root / "200" / std::to_string(Node3::kClassId) / "0";
    auto const snapshot_node2 =
        writable_root / "200" / std::to_string(Node2::kClassId) / "0";
    auto const snapshot_node =
        writable_root / "200" /
        std::to_string(apptraverse::Node::kClassId) / "0";

    APPTRAVERSE_CHECK(PathExists(snapshot_node3));
    APPTRAVERSE_CHECK(PathExists(snapshot_node2));
    APPTRAVERSE_CHECK(PathExists(snapshot_node));
    APPTRAVERSE_CHECK(ReadFileBytes(snapshot_node3, node3_layer_before));
    APPTRAVERSE_CHECK(ReadFileBytes(snapshot_node2, node2_layer_before));
    APPTRAVERSE_CHECK(ReadFileBytes(snapshot_node, node_layer_before));
    APPTRAVERSE_CHECK(!node3_layer_before.empty());
    APPTRAVERSE_CHECK(!node2_layer_before.empty());

    APPTRAVERSE_CHECK(node3->SetValue3(ae::ObjId{kUnusedSecondSnapshotId},
                                       ae::ObjId{kSetValue3EventId},
                                       std::int32_t{99}));
    APPTRAVERSE_CHECK(node3->object_id().id() == kNode1BId);
    APPTRAVERSE_CHECK(node3->value3() == 99);
    APPTRAVERSE_CHECK(node3->base_snapshot_id_for_test().id() ==
                      kBaseSnapshotId);
    APPTRAVERSE_CHECK(node3->journal_size_for_test() == 2);
    APPTRAVERSE_CHECK(node3->set_object_apply_calls() == 1);
    APPTRAVERSE_CHECK(node3->set_value3_apply_calls() == 1);

    APPTRAVERSE_CHECK(!PathExists(writable_root / "201"));

    std::vector<std::uint8_t> node3_layer_after;
    std::vector<std::uint8_t> node2_layer_after;
    std::vector<std::uint8_t> node_layer_after;
    APPTRAVERSE_CHECK(ReadFileBytes(snapshot_node3, node3_layer_after));
    APPTRAVERSE_CHECK(ReadFileBytes(snapshot_node2, node2_layer_after));
    APPTRAVERSE_CHECK(ReadFileBytes(snapshot_node, node_layer_after));
    APPTRAVERSE_CHECK(node3_layer_after == node3_layer_before);
    APPTRAVERSE_CHECK(node2_layer_after == node2_layer_before);
    APPTRAVERSE_CHECK(node_layer_after == node_layer_before);

    node3.Save();

    APPTRAVERSE_CHECK(IsDirectory(writable_root / "1"));
    APPTRAVERSE_CHECK(IsDirectory(writable_root / "100"));
    APPTRAVERSE_CHECK(IsDirectory(writable_root / "101"));
    APPTRAVERSE_CHECK(IsDirectory(writable_root / "200"));
    APPTRAVERSE_CHECK(!PathExists(writable_root / "2"));
    APPTRAVERSE_CHECK(!PathExists(writable_root / "3"));
    APPTRAVERSE_CHECK(!PathExists(writable_root / "4"));
    APPTRAVERSE_CHECK(!PathExists(writable_root / "5"));
    APPTRAVERSE_CHECK(!PathExists(writable_root / "201"));

    node3->CorruptObjectForTest(ae::ObjId{kNode1AId});
    node3->CorruptValue3ForTest(999);
    APPTRAVERSE_CHECK(node3->object_id().id() == kNode1AId);
    APPTRAVERSE_CHECK(node3->value3() == 999);
    node3.Save();
  }

  {
    ae::Domain domain{ae::Now(), storage};

    auto node3 = Node3::ptr::Declare(
        ae::CreateWith{domain}.with_id(kNode3Id));
    node3.Load();
    APPTRAVERSE_CHECK(node3);
    APPTRAVERSE_CHECK(node3->object_id().id() == kNode1BId);
    APPTRAVERSE_CHECK(node3->object());
    APPTRAVERSE_CHECK(node3->object()->marker() == 222);
    APPTRAVERSE_CHECK(node3->value3() == 99);
    APPTRAVERSE_CHECK(node3->base_snapshot_id_for_test().id() ==
                      kBaseSnapshotId);
    APPTRAVERSE_CHECK(node3->journal_size_for_test() == 2);
    APPTRAVERSE_CHECK(node3->set_object_apply_calls() == 1);
    APPTRAVERSE_CHECK(node3->set_value3_apply_calls() == 1);

    APPTRAVERSE_CHECK(!PathExists(writable_root / "201"));

    auto const snapshot_node3 =
        writable_root / "200" / std::to_string(Node3::kClassId) / "0";
    auto const snapshot_node2 =
        writable_root / "200" / std::to_string(Node2::kClassId) / "0";
    auto const snapshot_node =
        writable_root / "200" /
        std::to_string(apptraverse::Node::kClassId) / "0";

    std::vector<std::uint8_t> node3_layer_after;
    std::vector<std::uint8_t> node2_layer_after;
    std::vector<std::uint8_t> node_layer_after;
    APPTRAVERSE_CHECK(ReadFileBytes(snapshot_node3, node3_layer_after));
    APPTRAVERSE_CHECK(ReadFileBytes(snapshot_node2, node2_layer_after));
    APPTRAVERSE_CHECK(ReadFileBytes(snapshot_node, node_layer_after));
    APPTRAVERSE_CHECK(node3_layer_after == node3_layer_before);
    APPTRAVERSE_CHECK(node2_layer_after == node2_layer_before);
    APPTRAVERSE_CHECK(node_layer_after == node_layer_before);

    APPTRAVERSE_CHECK(IsDirectory(writable_root / "100"));
    APPTRAVERSE_CHECK(IsDirectory(writable_root / "101"));
  }

  APPTRAVERSE_CHECK(!PathExists(writable_root / "2"));
  APPTRAVERSE_CHECK(!PathExists(writable_root / "3"));
  APPTRAVERSE_CHECK(!PathExists(writable_root / "4"));
  APPTRAVERSE_CHECK(!PathExists(writable_root / "5"));
  APPTRAVERSE_CHECK(!PathExists(writable_root / "201"));

  return 0;
}
