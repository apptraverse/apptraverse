#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"

#include "apptraverse/control_text_edit.h"
#include "apptraverse/node.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

void SerializeMaterializedIncrementalPublication(TextEdit const& edit, ByteSink& out) {
  auto& node = const_cast<Node&>(static_cast<Node const&>(edit));
  Node::ptr saved_base = std::move(node.base);
  std::vector<EventRecord> saved_journal = std::move(node.journal);
  node.base = Node::ptr{};
  node.journal.clear();
  SerializeIncrementalNodePublication(edit, out);
  node.base = std::move(saved_base);
  node.journal = std::move(saved_journal);
}

ByteSink PacketFor(TextEdit const& edit) {
  ByteSink sink;
  SerializeMaterializedIncrementalPublication(edit, sink);
  return sink;
}

void ApplyPacket(ByteSource& in, ae::Domain& domain, ae::IDomainStorage& storage) {
  (void)ApplyIncrementalPublication(in, domain, storage);
}

void RunTextEditStaleIncrementalPublicationTest() {
  EnsureTextEditControlRegistration();
  ae::RamDomainStorage model_storage;
  ae::RamDomainStorage ui_storage;
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};

  ae::ObjId const shared_id{0xA11CE002u};
  auto model_edit = TextEdit::ptr::Create(ae::CreateWith{model_domain}.with_id(shared_id));
  InitializeRuntimeNode(*model_edit);

  auto ui_edit = TextEdit::ptr::Create(ae::CreateWith{ui_domain}.with_id(shared_id));
  InitializeRuntimeNode(*ui_edit);
  CHECK(ui_edit->obj_id == model_edit->obj_id);

  ByteSink baseline_snapshot;
  SerializePersistentModelSnapshot(*model_edit, baseline_snapshot);
  ByteSource baseline_in{baseline_snapshot.bytes.data(), baseline_snapshot.bytes.size(), 0};
  LoadPersistentModelSnapshot(baseline_in, ui_domain, ui_storage, *ui_edit);

  model_edit->RequestInsert("A");
  ByteSink p1 = PacketFor(*model_edit);
  ByteSource in1{p1.bytes.data(), p1.bytes.size(), 0};
  ApplyPacket(in1, ui_domain, ui_storage);
  CHECK(ui_edit->text == "A");
  std::uint64_t const g1 = ui_edit->Generation();

  model_edit->RequestInsert("B");
  ByteSink p2 = PacketFor(*model_edit);
  std::uint64_t const g2 = model_edit->Generation();
  CHECK(g2 > g1);

  ByteSource in2{p2.bytes.data(), p2.bytes.size(), 0};
  ApplyPacket(in2, ui_domain, ui_storage);
  CHECK(ui_edit->text == "AB");
  CHECK(ui_edit->Generation() == g2);
  CHECK(ui_edit->caret == model_edit->caret);
  CHECK(ui_edit->selection_anchor == model_edit->selection_anchor);

  ByteSource stale1{p1.bytes.data(), p1.bytes.size(), 0};
  ApplyPacket(stale1, ui_domain, ui_storage);
  CHECK(ui_edit->text == "AB");
  CHECK(ui_edit->Generation() == g2);

  ByteSource dup2{p2.bytes.data(), p2.bytes.size(), 0};
  ApplyPacket(dup2, ui_domain, ui_storage);
  CHECK(ui_edit->text == "AB");
  CHECK(ui_edit->Generation() == g2);

  model_edit->RequestInsert("C");
  ByteSink p3 = PacketFor(*model_edit);
  ByteSource in3{p3.bytes.data(), p3.bytes.size(), 0};
  ApplyPacket(in3, ui_domain, ui_storage);
  CHECK(ui_edit->text == "ABC");
  CHECK(ui_edit->Generation() == model_edit->Generation());

  ByteSource out_of_order{p1.bytes.data(), p1.bytes.size(), 0};
  ApplyPacket(out_of_order, ui_domain, ui_storage);
  CHECK(ui_edit->text == "ABC");
  CHECK(ui_edit->Generation() == model_edit->Generation());

  auto reloaded = ui_domain.Find(model_edit->obj_id);
  CHECK(reloaded);
  auto* reloaded_edit = reloaded.as<TextEdit>();
  CHECK(reloaded_edit != nullptr);
  CHECK(reloaded_edit->text == "ABC");
  CHECK(reloaded_edit->Generation() == model_edit->Generation());
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::RunTextEditStaleIncrementalPublicationTest();
  std::cerr << "text_edit_stale_incremental_publication_test OK\n";
  return 0;
}
