#include <concepts>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <system_error>
#include <type_traits>

#include "aether/obj/domain.h"

#include "apptraverse/versioned_directory_storage.h"

#include "type_owned_factory_fixture.h"

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

using apptraverse::test::HandlerFactory;
using apptraverse::test::HandlerNode;
using apptraverse::test::RuntimeObject;
using apptraverse::test::SetValueEvent;

static_assert(!std::is_constructible_v<SetValueEvent, ae::ObjProp, std::int32_t>);
static_assert(
    !std::is_constructible_v<RuntimeObject, ae::ObjProp, std::int32_t>);
static_assert(!std::is_constructible_v<HandlerFactory, ae::ObjProp,
                                       SetValueEvent::ptr, RuntimeObject::ptr>);
static_assert(!std::is_constructible_v<HandlerNode, ae::ObjProp, ae::ObjId,
                                       std::int32_t>);

template <typename FactoryT>
concept ExternalCanCreateSetValueEvent =
    requires(FactoryT& factory, ae::ObjId id, std::int32_t value) {
      factory.CreateSetValueEvent(id, value);
    };

template <typename FactoryT>
concept ExternalCanCreateRuntimeObject =
    requires(FactoryT& factory, ae::ObjId id, std::int32_t value) {
      factory.CreateRuntimeObject(id, value);
    };

template <typename FactoryT>
constexpr bool FactoryCreateMethodsArePrivate() {
  if constexpr (ExternalCanCreateSetValueEvent<FactoryT> ||
                ExternalCanCreateRuntimeObject<FactoryT>) {
    return false;
  }
  return true;
}

static_assert(FactoryCreateMethodsArePrivate<HandlerFactory>());

}  // namespace

int main(int argc, char** argv) {
  using apptraverse::test::kHandlerFactoryId;
  using apptraverse::test::kHandlerNodeId;
  using apptraverse::test::kRuntimeEventId;
  using apptraverse::test::kRuntimeObjectId;
  using apptraverse::test::kRuntimeObjectPrototypeId;
  using apptraverse::test::kSecondRuntimeEventId;
  using apptraverse::test::kSetValueEventPrototypeId;

  if (argc < 3) {
    std::cerr << "usage: apptraverse_type_owned_factory_runtime "
                 "<writable-root> <base-root>\n";
    return 1;
  }

  std::filesystem::path const writable_root{argv[1]};
  std::filesystem::path const base_root{argv[2]};

  apptraverse::VersionedDirectoryStorage storage{writable_root, {base_root}};

  {
    ae::Domain domain{ae::Now(), storage};

    auto handler = HandlerNode::ptr::Declare(
        ae::CreateWith{domain}.with_id(kHandlerNodeId));
    handler.Load();
    APPTRAVERSE_CHECK(handler);
    APPTRAVERSE_CHECK(handler->value() == 10);
    APPTRAVERSE_CHECK(handler->factory_id().id() == kHandlerFactoryId);

    APPTRAVERSE_CHECK(
        handler->SetValue(ae::ObjId{kRuntimeEventId}, std::int32_t{42}));
    APPTRAVERSE_CHECK(handler->value() == 42);
    APPTRAVERSE_CHECK(handler->last_event());
    APPTRAVERSE_CHECK(handler->last_event().id().id() == kRuntimeEventId);
    APPTRAVERSE_CHECK(handler->last_event()->value() == 42);

    APPTRAVERSE_CHECK(handler->CreateRuntimeObject(
        ae::ObjId{kRuntimeObjectId}, std::int32_t{123}));
    APPTRAVERSE_CHECK(handler->runtime_object());
    APPTRAVERSE_CHECK(handler->runtime_object().id().id() == kRuntimeObjectId);
    APPTRAVERSE_CHECK(handler->runtime_object()->prototype_marker() == 77);
    APPTRAVERSE_CHECK(handler->runtime_object()->runtime_value() == 123);

    auto event_prototype = SetValueEvent::ptr::Declare(
        ae::CreateWith{domain}.with_id(kSetValueEventPrototypeId));
    event_prototype.Load();
    APPTRAVERSE_CHECK(event_prototype);
    APPTRAVERSE_CHECK(event_prototype->value() == 0);

    auto object_prototype = RuntimeObject::ptr::Declare(
        ae::CreateWith{domain}.with_id(kRuntimeObjectPrototypeId));
    object_prototype.Load();
    APPTRAVERSE_CHECK(object_prototype);
    APPTRAVERSE_CHECK(object_prototype->prototype_marker() == 77);
    APPTRAVERSE_CHECK(object_prototype->runtime_value() == 0);

    handler.Save();
  }

  APPTRAVERSE_CHECK(IsDirectory(writable_root / "1"));
  APPTRAVERSE_CHECK(IsDirectory(writable_root / "100"));
  APPTRAVERSE_CHECK(IsDirectory(writable_root / "101"));
  APPTRAVERSE_CHECK(!PathExists(writable_root / "2"));
  APPTRAVERSE_CHECK(!PathExists(writable_root / "3"));
  APPTRAVERSE_CHECK(!PathExists(writable_root / "4"));

  {
    ae::Domain domain{ae::Now(), storage};

    auto handler = HandlerNode::ptr::Declare(
        ae::CreateWith{domain}.with_id(kHandlerNodeId));
    handler.Load();
    APPTRAVERSE_CHECK(handler);
    APPTRAVERSE_CHECK(handler->value() == 42);
    APPTRAVERSE_CHECK(handler->factory_id().id() == kHandlerFactoryId);
    APPTRAVERSE_CHECK(handler->last_event());
    APPTRAVERSE_CHECK(handler->last_event().id().id() == kRuntimeEventId);
    APPTRAVERSE_CHECK(handler->last_event()->value() == 42);
    APPTRAVERSE_CHECK(handler->runtime_object());
    APPTRAVERSE_CHECK(handler->runtime_object().id().id() == kRuntimeObjectId);
    APPTRAVERSE_CHECK(handler->runtime_object()->prototype_marker() == 77);
    APPTRAVERSE_CHECK(handler->runtime_object()->runtime_value() == 123);

    APPTRAVERSE_CHECK(
        handler->SetValue(ae::ObjId{kSecondRuntimeEventId}, std::int32_t{84}));
    APPTRAVERSE_CHECK(handler->value() == 84);
    APPTRAVERSE_CHECK(handler->last_event());
    APPTRAVERSE_CHECK(handler->last_event().id().id() == kSecondRuntimeEventId);
    APPTRAVERSE_CHECK(handler->last_event()->value() == 84);

    handler.Save();
  }

  APPTRAVERSE_CHECK(IsDirectory(writable_root / "102"));
  APPTRAVERSE_CHECK(!PathExists(writable_root / "2"));
  APPTRAVERSE_CHECK(!PathExists(writable_root / "3"));
  APPTRAVERSE_CHECK(!PathExists(writable_root / "4"));

  return 0;
}
