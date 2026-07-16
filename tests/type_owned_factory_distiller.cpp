#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <system_error>

#include "aether/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"

#include "type_owned_factory_fixture.h"

namespace {

bool IsDirectory(std::filesystem::path const& path) {
  std::error_code ec;
  auto const is_dir = std::filesystem::is_directory(path, ec);
  return !ec && is_dir;
}

}  // namespace

int main(int argc, char** argv) {
  using apptraverse::test::HandlerFactory;
  using apptraverse::test::HandlerNode;
  using apptraverse::test::RuntimeObject;
  using apptraverse::test::SetValueEvent;
  using apptraverse::test::kHandlerFactoryId;
  using apptraverse::test::kHandlerNodeId;
  using apptraverse::test::kRuntimeObjectPrototypeId;
  using apptraverse::test::kSetValueEventPrototypeId;

  if (argc < 2) {
    std::cerr << "usage: apptraverse_type_owned_factory_distiller <base-root>\n";
    return 1;
  }

  std::filesystem::path const base_root{argv[1]};

  apptraverse::DirectoryDomainStorage storage{base_root};
  ae::Domain domain{ae::Now(), storage};

  auto event_prototype = SetValueEvent::ptr::Create(
      ae::CreateWith{domain}.with_id(kSetValueEventPrototypeId),
      std::int32_t{0});
  APPTRAVERSE_CHECK(event_prototype);

  auto object_prototype = RuntimeObject::ptr::Create(
      ae::CreateWith{domain}.with_id(kRuntimeObjectPrototypeId),
      std::int32_t{77});
  APPTRAVERSE_CHECK(object_prototype);
  APPTRAVERSE_CHECK(object_prototype->prototype_marker() == 77);
  APPTRAVERSE_CHECK(object_prototype->runtime_value() == 0);

  auto factory = HandlerFactory::ptr::Create(
      ae::CreateWith{domain}.with_id(kHandlerFactoryId), event_prototype,
      object_prototype);
  APPTRAVERSE_CHECK(factory);

  auto handler = HandlerNode::ptr::Create(
      ae::CreateWith{domain}.with_id(kHandlerNodeId),
      ae::ObjId{kHandlerFactoryId}, std::int32_t{10});
  APPTRAVERSE_CHECK(handler);
  APPTRAVERSE_CHECK(handler->value() == 10);
  APPTRAVERSE_CHECK(handler->factory_id().id() == kHandlerFactoryId);

  factory.Save();
  handler.Save();

  APPTRAVERSE_CHECK(IsDirectory(base_root / "1"));
  APPTRAVERSE_CHECK(IsDirectory(base_root / "2"));
  APPTRAVERSE_CHECK(IsDirectory(base_root / "3"));
  APPTRAVERSE_CHECK(IsDirectory(base_root / "4"));

  return 0;
}
