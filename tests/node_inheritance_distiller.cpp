#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <system_error>

#include "aether/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"

#include "node_inheritance_fixture.h"

namespace {

bool IsDirectory(std::filesystem::path const& path) {
  std::error_code ec;
  auto const is_dir = std::filesystem::is_directory(path, ec);
  return !ec && is_dir;
}

}  // namespace

int main(int argc, char** argv) {
  using apptraverse::test::kNode1AId;
  using apptraverse::test::kNode1BId;
  using apptraverse::test::kNode2FactoryId;
  using apptraverse::test::kNode3FactoryId;
  using apptraverse::test::kNode3Id;
  using apptraverse::test::kSetObjectEventPrototypeId;
  using apptraverse::test::kSetValue3EventPrototypeId;
  using apptraverse::test::Node1;
  using apptraverse::test::Node2Factory;
  using apptraverse::test::Node3;
  using apptraverse::test::Node3Factory;
  using apptraverse::test::SetObjectEvent;
  using apptraverse::test::SetValue3Event;

  if (argc < 2) {
    std::cerr << "usage: apptraverse_node_inheritance_distiller <base-root>\n";
    return 1;
  }

  std::filesystem::path const base_root{argv[1]};

  apptraverse::DirectoryDomainStorage storage{base_root};
  ae::Domain domain{ae::Now(), storage};

  auto node1_a = Node1::ptr::Create(
      ae::CreateWith{domain}.with_id(kNode1AId), std::int32_t{111});
  APPTRAVERSE_CHECK(node1_a);
  APPTRAVERSE_CHECK(node1_a->marker() == 111);

  auto node1_b = Node1::ptr::Create(
      ae::CreateWith{domain}.with_id(kNode1BId), std::int32_t{222});
  APPTRAVERSE_CHECK(node1_b);
  APPTRAVERSE_CHECK(node1_b->marker() == 222);

  auto set_object_prototype = SetObjectEvent::ptr::Create(
      ae::CreateWith{domain}.with_id(kSetObjectEventPrototypeId), ae::ObjId{});
  APPTRAVERSE_CHECK(set_object_prototype);

  auto set_value3_prototype = SetValue3Event::ptr::Create(
      ae::CreateWith{domain}.with_id(kSetValue3EventPrototypeId),
      std::int32_t{0});
  APPTRAVERSE_CHECK(set_value3_prototype);

  auto node2_factory = Node2Factory::ptr::Create(
      ae::CreateWith{domain}.with_id(kNode2FactoryId), set_object_prototype);
  APPTRAVERSE_CHECK(node2_factory);

  auto node3_factory = Node3Factory::ptr::Create(
      ae::CreateWith{domain}.with_id(kNode3FactoryId), set_value3_prototype);
  APPTRAVERSE_CHECK(node3_factory);

  auto node3 = Node3::ptr::Create(
      ae::CreateWith{domain}.with_id(kNode3Id), ae::ObjId{kNode2FactoryId},
      node1_a, ae::ObjId{kNode3FactoryId}, std::int32_t{30});
  APPTRAVERSE_CHECK(node3);
  APPTRAVERSE_CHECK(node3->object_id().id() == kNode1AId);
  APPTRAVERSE_CHECK(node3->value3() == 30);
  APPTRAVERSE_CHECK(!node3->base_snapshot_id_for_test().IsValid());
  APPTRAVERSE_CHECK(node3->journal_size_for_test() == 0);

  node1_a.Save();
  node1_b.Save();
  node2_factory.Save();
  node3_factory.Save();
  node3.Save();

  APPTRAVERSE_CHECK(IsDirectory(base_root / "1"));
  APPTRAVERSE_CHECK(IsDirectory(base_root / "2"));
  APPTRAVERSE_CHECK(IsDirectory(base_root / "3"));
  APPTRAVERSE_CHECK(IsDirectory(base_root / "4"));
  APPTRAVERSE_CHECK(IsDirectory(base_root / "5"));
  APPTRAVERSE_CHECK(IsDirectory(base_root / "10"));
  APPTRAVERSE_CHECK(IsDirectory(base_root / "11"));

  return 0;
}
