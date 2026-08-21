#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/domain_snapshot_io.h"
#include "apptraverse/node.h"
#include "apptraverse/object_graph_copy.h"
#include "apptraverse/object_macros.h"

#include "chat_component.h"
#include "chat_component_graph.h"
#include "model/chat.h"
#include "model/chat_component_registration.h"
#include "model/chat_events.h"
#include "model/chat_peer_set.h"
#include "model/client.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

std::filesystem::path TempRoot(char const* name) {
  auto path = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(path);
  return path;
}

void TestEmptyDirectoryLoadsEmptyRam() {
  ResetDomainSnapshotIoStats();
  auto root = TempRoot("apptraverse_snapshot_empty");
  std::filesystem::create_directories(root);
  ae::RamDomainStorage ram;
  LoadDirectorySnapshot(root, ram);
  CHECK(ram.state.empty());
  CHECK(GetDomainSnapshotIoStats().load_calls == 1);
  CHECK(GetDomainSnapshotIoStats().save_calls == 0);
  std::filesystem::remove_all(root);
}

void TestRoundTripDirectoryToRamAndBack() {
  ResetDomainSnapshotIoStats();
  auto root = TempRoot("apptraverse_snapshot_roundtrip");
  {
    DirectoryDomainStorage disk{root};
    ae::Domain domain{ae::Now(), disk};
    auto node = Node::ptr::Create(ae::CreateWith{domain}.with_id(42));
    node.Save();
  }
  ae::RamDomainStorage ram;
  LoadDirectorySnapshot(root, ram);
  CHECK(!ram.state.empty());
  CHECK(GetDomainSnapshotIoStats().load_calls == 1);

  auto out = TempRoot("apptraverse_snapshot_roundtrip_out");
  SaveDirectorySnapshot(ram, out);
  CHECK(GetDomainSnapshotIoStats().save_calls == 1);

  ae::RamDomainStorage ram2;
  LoadDirectorySnapshot(out, ram2);
  CHECK(ram2.state.size() == ram.state.size());
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(out);
}

void TestRuntimeSaveTouchesOnlyRam() {
  ResetDomainSnapshotIoStats();
  auto root = TempRoot("apptraverse_snapshot_ram_only");
  std::filesystem::create_directories(root);

  ae::RamDomainStorage ram;
  LoadDirectorySnapshot(root, ram);  // empty
  ae::Domain domain{ae::Now(), ram};
  auto graph = chat::BuildChatComponentGraph(domain, "Snap");
  graph.chat.Save();

  // Snapshot directory must remain empty (no runtime file writes).
  std::error_code ec;
  auto it = std::filesystem::directory_iterator{root, ec};
  CHECK(std::filesystem::begin(it) == std::filesystem::end(it));
  CHECK(GetDomainSnapshotIoStats().save_calls == 0);

  // Mutate RAM via another Save.
  auto event = AddMessageEvent::ptr::Create(ae::CreateWith{domain});
  event->author = graph.local_client;
  event->text = "ram-msg";
  graph.chat->Commit(event);
  graph.chat.Save();
  CHECK(GetDomainSnapshotIoStats().save_calls == 0);

  SaveDirectorySnapshot(ram, root);
  CHECK(GetDomainSnapshotIoStats().save_calls == 1);
  CHECK(std::filesystem::exists(root / std::to_string(graph.chat.id().id()), ec));

  // Restart from snapshot.
  ae::RamDomainStorage ram2;
  LoadDirectorySnapshot(root, ram2);
  ae::Domain domain2{ae::Now(), ram2};
  auto chat = Chat::ptr::Declare(
      ae::CreateWith{domain2}.with_id(graph.chat.id()));
  chat.Load();
  CHECK(chat.is_loaded());
  bool found = false;
  for (auto const& record : chat->journal) {
    if (!record.event.is_valid()) {
      continue;
    }
    auto msg = AddMessageEvent::ptr{record.event};
    msg.Load();
    if (msg.is_loaded() && msg->text == "ram-msg") {
      found = true;
    }
  }
  CHECK(found);
  std::filesystem::remove_all(root);
}

void TestRemovedObjectDoesNotReappear() {
  ResetDomainSnapshotIoStats();
  auto root = TempRoot("apptraverse_snapshot_remove");
  ae::RamDomainStorage ram;
  {
    ae::Domain domain{ae::Now(), ram};
    auto keep = Node::ptr::Create(ae::CreateWith{domain}.with_id(10));
    keep.Save();
    auto drop = Node::ptr::Create(ae::CreateWith{domain}.with_id(11));
    drop.Save();
  }
  ram.Remove(ae::ObjId{11});
  SaveDirectorySnapshot(ram, root);

  ae::RamDomainStorage ram2;
  LoadDirectorySnapshot(root, ram2);
  auto keep_it = ram2.state.find(ae::ObjId{10});
  CHECK(keep_it != ram2.state.end());
  CHECK(keep_it->second.has_value());
  auto drop_it = ram2.state.find(ae::ObjId{11});
  // Tombstone or absent both mean object data is gone.
  CHECK(drop_it == ram2.state.end() || !drop_it->second.has_value());
  std::filesystem::remove_all(root);
}

void TestCopySeesLatestRamWithoutDisk() {
  ResetDomainSnapshotIoStats();
  auto root = TempRoot("apptraverse_snapshot_copy");
  std::filesystem::create_directories(root);
  ae::RamDomainStorage ram;
  ae::Domain domain{ae::Now(), ram};
  auto graph = chat::BuildChatComponentGraph(domain, "Copy");
  graph.chat.Save();

  auto event = AddMessageEvent::ptr::Create(ae::CreateWith{domain});
  event->author = graph.local_client;
  event->text = "fresh-ram";
  graph.chat->Commit(event);
  graph.chat.Save();  // RAM only

  ae::RamDomainStorage build_storage;
  ae::Domain build_domain{ae::Now(), build_storage};
  // CopyObjectGraph saves source into live storage then read-through copies.
  apptraverse::CopyObjectGraph(event, ram, build_domain, build_storage,
                               SharedCopyMode::kCopyLoadedTargets);
  auto copied =
      Event::ptr::Declare(ae::CreateWith{build_domain}.with_id(event.id()));
  copied.Load();
  CHECK(copied.is_loaded());
  auto msg = AddMessageEvent::ptr{copied};
  msg.Load();
  CHECK(msg.is_loaded());
  CHECK(msg->text == "fresh-ram");
  CHECK(GetDomainSnapshotIoStats().save_calls == 0);
  std::filesystem::remove_all(root);
}

void TestShutdownSaveCountExactlyOne() {
  ResetDomainSnapshotIoStats();
  auto root = TempRoot("apptraverse_snapshot_once");
  ae::RamDomainStorage ram;
  {
    ae::Domain domain{ae::Now(), ram};
    auto node = Node::ptr::Create(ae::CreateWith{domain}.with_id(7));
    node.Save();
  }
  CHECK(GetDomainSnapshotIoStats().save_calls == 0);
  SaveDirectorySnapshot(ram, root);
  CHECK(GetDomainSnapshotIoStats().save_calls == 1);
  std::filesystem::remove_all(root);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureChatComponentRegistration();
  apptraverse::test::TestEmptyDirectoryLoadsEmptyRam();
  apptraverse::test::TestRoundTripDirectoryToRamAndBack();
  apptraverse::test::TestRuntimeSaveTouchesOnlyRam();
  apptraverse::test::TestRemovedObjectDoesNotReappear();
  apptraverse::test::TestCopySeesLatestRamWithoutDisk();
  apptraverse::test::TestShutdownSaveCountExactlyOne();
  std::cout << "domain_snapshot_io_test OK\n";
  return 0;
}
