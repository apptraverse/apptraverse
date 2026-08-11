#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_graph_copy.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

class XferPeer;
class XferDoc;
class XferBump;

class XferPeer : public NodeFor<XferPeer> {
  APPTRAVERSE_OBJECT(XferPeer, Node, 0)
 protected:
  XferPeer() = default;
 public:
  explicit XferPeer(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(name))
  std::string name;
};

class XferDoc : public NodeFor<XferDoc> {
  APPTRAVERSE_OBJECT(XferDoc, Node, 0)
 protected:
  XferDoc() = default;
 public:
  explicit XferDoc(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(value), AE_MMBR(peer), AE_MMBR(local_note))
  std::int32_t value{0};
  SharedPtr<XferPeer> peer;
  LocalPtr<XferPeer> local_note;
  void Apply(XferBump const& event);
};

class XferBump : public EventFor<XferDoc, XferBump> {
  APPTRAVERSE_OBJECT(XferBump, Event, 0)
 protected:
  XferBump() = default;
 public:
  explicit XferBump(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(delta))
  std::int32_t delta{0};
};

void XferDoc::Apply(XferBump const& event) { value += event.delta; }

APPTRAVERSE_REGISTER(XferPeer);
APPTRAVERSE_REGISTER(XferDoc);
APPTRAVERSE_REGISTER(XferBump);

template <typename SourceStorage, typename TargetStorage>
void RunTransfer(SourceStorage& source_storage, TargetStorage& target_storage,
                 std::filesystem::path const* /*unused*/ = nullptr) {
  ae::Domain source_domain{ae::Now(), source_storage};
  auto peer_base =
      XferPeer::ptr::Create(ae::CreateWith{source_domain}.with_id(10));
  auto peer = XferPeer::ptr::Create(ae::CreateWith{source_domain}.with_id(11));
  peer->base = peer_base;
  peer->name = "Alice";
  peer->CaptureBaseState();
  peer.Save();

  auto local =
      XferPeer::ptr::Create(ae::CreateWith{source_domain}.with_id(12));
  local->name = "secret-local";
  local.Save();

  auto doc_base =
      XferDoc::ptr::Create(ae::CreateWith{source_domain}.with_id(20));
  auto doc = XferDoc::ptr::Create(ae::CreateWith{source_domain}.with_id(21));
  doc->base = doc_base;
  doc->peer = peer;
  doc->local_note = local;
  doc->CaptureBaseState();
  auto bump = XferBump::ptr::Create(ae::CreateWith{source_domain}.with_id(30));
  bump->delta = 5;
  doc->Commit(bump);
  doc.Save();

  auto const source_value = doc->value;
  auto const source_local_id = local.id();

  ae::Domain target_domain{ae::Now(), target_storage};
  SyncReplica target{target_domain, target_storage, doc.id()};
  ImportObjectGraph(doc, source_storage, target,
                    SharedCopyMode::kCopyLoadedTargets);

  auto loaded =
      XferDoc::ptr::Declare(ae::CreateWith{target_domain}.with_id(doc.id()));
  loaded.Load();
  CHECK(loaded.is_loaded());
  CHECK(loaded->value == source_value);
  CHECK(loaded->peer.is_loaded());
  CHECK(loaded->peer->name == "Alice");
  CHECK(!loaded->local_note.is_valid() || !loaded->local_note.is_loaded());

  // Source LocalPtr still intact.
  doc.Load();
  CHECK(doc->local_note.is_valid());
  CHECK(doc->local_note.id() == source_local_id);
}

void TestRamToRam() {
  ae::RamDomainStorage a;
  ae::RamDomainStorage b;
  RunTransfer(a, b);
}

void TestDirectoryToRam() {
  auto root = std::filesystem::temp_directory_path() /
              "apptraverse_xfer_dir_to_ram";
  std::filesystem::remove_all(root);
  DirectoryDomainStorage a{root};
  ae::RamDomainStorage b;
  RunTransfer(a, b);
  std::filesystem::remove_all(root);
}

void TestRamToDirectory() {
  auto root = std::filesystem::temp_directory_path() /
              "apptraverse_xfer_ram_to_dir";
  std::filesystem::remove_all(root);
  ae::RamDomainStorage a;
  DirectoryDomainStorage b{root};
  RunTransfer(a, b);
  std::filesystem::remove_all(root);
}

void TestDirectoryToDirectory() {
  auto root_a = std::filesystem::temp_directory_path() /
                "apptraverse_xfer_dir_a";
  auto root_b = std::filesystem::temp_directory_path() /
                "apptraverse_xfer_dir_b";
  std::filesystem::remove_all(root_a);
  std::filesystem::remove_all(root_b);
  DirectoryDomainStorage a{root_a};
  DirectoryDomainStorage b{root_b};
  RunTransfer(a, b);
  std::filesystem::remove_all(root_a);
  std::filesystem::remove_all(root_b);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestRamToRam();
  apptraverse::test::TestDirectoryToRam();
  apptraverse::test::TestRamToDirectory();
  apptraverse::test::TestDirectoryToDirectory();
  std::cout << "generic_storage_transfer_test OK\n";
  return 0;
}
