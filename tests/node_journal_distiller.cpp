#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <system_error>

#include "aether/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"

#include "node_journal_fixture.h"

namespace {

bool IsDirectory(std::filesystem::path const& path) {
  std::error_code ec;
  auto const is_dir = std::filesystem::is_directory(path, ec);
  return !ec && is_dir;
}

}  // namespace

int main(int argc, char** argv) {
  using apptraverse::test::JournalFactory;
  using apptraverse::test::JournalNode;
  using apptraverse::test::JournalSetValueEvent;
  using apptraverse::test::UnrelatedEvent;
  using apptraverse::test::kJournalEventPrototypeId;
  using apptraverse::test::kJournalFactoryId;
  using apptraverse::test::kJournalNodeId;
  using apptraverse::test::kUnrelatedEventPrototypeId;

  if (argc < 2) {
    std::cerr << "usage: apptraverse_node_journal_distiller <base-root>\n";
    return 1;
  }

  std::filesystem::path const base_root{argv[1]};

  apptraverse::DirectoryDomainStorage storage{base_root};
  ae::Domain domain{ae::Now(), storage};

  auto event_prototype = JournalSetValueEvent::ptr::Create(
      ae::CreateWith{domain}.with_id(kJournalEventPrototypeId),
      std::int32_t{0});
  APPTRAVERSE_CHECK(event_prototype);

  auto unrelated_event_prototype = UnrelatedEvent::ptr::Create(
      ae::CreateWith{domain}.with_id(kUnrelatedEventPrototypeId));
  APPTRAVERSE_CHECK(unrelated_event_prototype);

  auto factory = JournalFactory::ptr::Create(
      ae::CreateWith{domain}.with_id(kJournalFactoryId), event_prototype);
  APPTRAVERSE_CHECK(factory);

  auto node = JournalNode::ptr::Create(
      ae::CreateWith{domain}.with_id(kJournalNodeId),
      ae::ObjId{kJournalFactoryId}, std::int32_t{10});
  APPTRAVERSE_CHECK(node);
  APPTRAVERSE_CHECK(node->value() == 10);
  APPTRAVERSE_CHECK(node->factory_id().id() == kJournalFactoryId);
  APPTRAVERSE_CHECK(!node->base_snapshot_id_for_test().IsValid());
  APPTRAVERSE_CHECK(node->journal_size_for_test() == 0);

  factory.Save();
  node.Save();
  unrelated_event_prototype.Save();

  APPTRAVERSE_CHECK(IsDirectory(base_root / "1"));
  APPTRAVERSE_CHECK(IsDirectory(base_root / "2"));
  APPTRAVERSE_CHECK(IsDirectory(base_root / "3"));
  APPTRAVERSE_CHECK(IsDirectory(base_root / "4"));

  return 0;
}
