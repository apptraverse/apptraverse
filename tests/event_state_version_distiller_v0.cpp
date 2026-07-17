#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <system_error>

#include "aether/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"

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

}  // namespace

int main(int argc, char** argv) {
  using apptraverse::test::AddValueEvent;
  using apptraverse::test::EventVersionFactory;
  using apptraverse::test::EventVersionNode;
  using apptraverse::test::kAddValueEventPrototypeId;
  using apptraverse::test::kEventVersionFactoryId;
  using apptraverse::test::kEventVersionNodeId;

  static_assert(AddValueEvent::kVersion == 0);

  if (argc < 2) {
    std::cerr
        << "usage: apptraverse_event_state_version_distiller_v0 <base-root>\n";
    return 1;
  }

  std::filesystem::path const base_root{argv[1]};

  apptraverse::DirectoryDomainStorage storage{base_root};
  ae::Domain domain{ae::Now(), storage};

  auto event_prototype = AddValueEvent::ptr::Create(
      ae::CreateWith{domain}.with_id(kAddValueEventPrototypeId),
      std::int64_t{0});
  APPTRAVERSE_CHECK(event_prototype);
  APPTRAVERSE_CHECK(event_prototype->legacy_delta_for_test() == 0);
  APPTRAVERSE_CHECK(event_prototype->logical_delta() == 0);

  auto factory = EventVersionFactory::ptr::Create(
      ae::CreateWith{domain}.with_id(kEventVersionFactoryId), event_prototype);
  APPTRAVERSE_CHECK(factory);

  auto node = EventVersionNode::ptr::Create(
      ae::CreateWith{domain}.with_id(kEventVersionNodeId),
      ae::ObjId{kEventVersionFactoryId}, std::int64_t{100});
  APPTRAVERSE_CHECK(node);
  APPTRAVERSE_CHECK(node->value_for_test() == 100);
  APPTRAVERSE_CHECK(node->factory_id().id() == kEventVersionFactoryId);

  factory.Save();
  node.Save();

  APPTRAVERSE_CHECK(IsDirectory(base_root / "1"));
  APPTRAVERSE_CHECK(IsDirectory(base_root / "2"));
  APPTRAVERSE_CHECK(IsDirectory(base_root / "3"));
  APPTRAVERSE_CHECK(PathExists(base_root / "3" /
                               std::to_string(AddValueEvent::kClassId) /
                               "0"));
  APPTRAVERSE_CHECK(!PathExists(base_root / "3" /
                                std::to_string(AddValueEvent::kClassId) /
                                "1"));

  return 0;
}
