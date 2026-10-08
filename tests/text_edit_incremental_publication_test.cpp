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

void RunTextEditIncrementalPublicationTest() {
  EnsureTextEditControlRegistration();
  ae::RamDomainStorage model_storage;
  ae::RamDomainStorage ui_storage;
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};

  auto model_edit = TextEdit::ptr::Create(ae::CreateWith{model_domain});
  InitializeRuntimeNode(*model_edit);

  ByteSink baseline_snapshot;
  SerializePersistentModelSnapshot(*model_edit, baseline_snapshot);

  auto ui_edit = TextEdit::ptr::Create(
      ae::CreateWith{ui_domain}.with_id(model_edit->obj_id));
  InitializeRuntimeNode(*ui_edit);
  CHECK(ui_edit->obj_id == model_edit->obj_id);
  ByteSource baseline_in{baseline_snapshot.bytes.data(), baseline_snapshot.bytes.size(), 0};
  LoadPersistentModelSnapshot(baseline_in, ui_domain, ui_storage, *ui_edit);
  CHECK(ui_edit->Generation() == model_edit->Generation());

  std::uint64_t const gen0 = model_edit->Generation();
  model_edit->RequestInsert("A");
  CHECK(model_edit->Generation() == gen0 + 1);

  ByteSink sink;
  SerializeMaterializedIncrementalPublication(*model_edit, sink);
  CHECK(sink.bytes.size() >= 4U + 8U + 4U);
  std::uint64_t envelope_generation = 0;
  std::memcpy(&envelope_generation, sink.bytes.data() + sizeof(std::uint32_t),
              sizeof(envelope_generation));
  CHECK(envelope_generation == model_edit->Generation());
  ByteSource in{sink.bytes.data(), sink.bytes.size(), 0};
  ae::Obj& updated =
      ApplyIncrementalPublication(in, ui_domain, ui_storage);
  CHECK(updated.obj_id == model_edit->obj_id);

  auto ui_handle = ui_domain.Find(model_edit->obj_id);
  CHECK(ui_handle);
  auto* ui_mirror = ui_handle.as<TextEdit>();
  CHECK(ui_mirror != nullptr);
  CHECK(ui_mirror->obj_id == model_edit->obj_id);
  CHECK(ui_mirror->text == "A");
  CHECK(ui_mirror->caret == model_edit->caret);
  CHECK(ui_mirror->selection_anchor == model_edit->selection_anchor);
  CHECK(static_cast<Node&>(updated).Generation() == envelope_generation);
  CHECK(ui_mirror->Generation() == model_edit->Generation());
  CHECK(ui_mirror->journal.empty());
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunTextEditIncrementalPublicationTest();
  std::cerr << "text_edit_incremental_publication_test OK\n";
  return 0;
}
