#include <cstdlib>
#include <iostream>
#include <string>

#include "aether-objects/domain_storage/ram_domain_storage.h"

#include "apptraverse/control_text_edit.h"
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

std::uint32_t LastEventClassId(TextEdit const& edit) {
  CHECK(!edit.journal.empty());
  CHECK(edit.journal.back().event.is_valid());
  return edit.journal.back().event->GetClassId();
}

}  // namespace

void RunTextEditJournalTest() {
  EnsureTextEditControlRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto edit = TextEdit::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*edit);

  auto expect_event = [&](auto const& request_fn, std::uint32_t class_id) {
    std::size_t const before = edit->journal.size();
    request_fn();
    CHECK(edit->journal.size() == before + 1);
    CHECK(LastEventClassId(*edit) == class_id);
  };

  expect_event([&] { edit->RequestInsert("A"); }, TextEditInsertEvent::kClassId);
  CHECK(edit->text == "A");

  expect_event([&] { edit->RequestSetCaret(1); }, TextEditSetCaretEvent::kClassId);

  expect_event([&] { edit->RequestSetSelection(0, 1); },
               TextEditSetSelectionEvent::kClassId);

  expect_event([&] { edit->RequestReplaceRange(0, 1, "B"); },
               TextEditReplaceRangeEvent::kClassId);
  CHECK(edit->text == "B");

  edit->RequestSetSelection(0, 1);
  expect_event([&] { edit->RequestInsert("!"); }, TextEditInsertEvent::kClassId);
  CHECK(edit->text == "!");

  expect_event([&] { edit->RequestDeleteBackward(1); },
               TextEditDeleteBackwardEvent::kClassId);
  CHECK(edit->text.empty());

  edit->RequestInsert("\xF0\x9F\x98\x80");
  expect_event([&] { edit->RequestDeleteBackward(1); },
               TextEditDeleteBackwardEvent::kClassId);
  CHECK(edit->text.empty());

  edit->RequestInsert("xy");
  edit->RequestSetSelection(0, 1);
  expect_event([&] { edit->RequestDeleteSelection(); },
               TextEditDeleteSelectionEvent::kClassId);
  CHECK(edit->text == "y");

  expect_event([&] { edit->RequestClear(); }, TextEditClearEvent::kClassId);
  CHECK(edit->text.empty());
  CHECK(edit->caret == 0);

  ByteSink snap;
  SerializePersistentModelSnapshot(*edit, snap);
  ae::RamDomainStorage load_storage;
  ae::Domain load_domain{load_storage};
  auto loaded = TextEdit::ptr::Create(
      ae::CreateWith{load_domain}.with_id(edit->obj_id));
  InitializeRuntimeNode(*loaded);
  ByteSource in;
  in.data = snap.bytes.data();
  in.size = snap.bytes.size();
  in.pos = 0;
  LoadPersistentModelSnapshot(in, load_domain, load_storage, *loaded);
  CHECK(loaded->text == edit->text);
  std::size_t const loaded_events = loaded->journal.size();
  loaded->RequestInsert("z");
  CHECK(loaded->journal.size() == loaded_events + 1);
  CHECK(loaded->text == "z");

  ByteSink snap2;
  SerializePersistentModelSnapshot(*loaded, snap2);
  ae::RamDomainStorage load_storage2;
  ae::Domain load_domain2{load_storage2};
  auto loaded2 = TextEdit::ptr::Create(
      ae::CreateWith{load_domain2}.with_id(edit->obj_id));
  InitializeRuntimeNode(*loaded2);
  ByteSource in2;
  in2.data = snap2.bytes.data();
  in2.size = snap2.bytes.size();
  in2.pos = 0;
  LoadPersistentModelSnapshot(in2, load_domain2, load_storage2, *loaded2);
  CHECK(loaded2->text == "z");
}

}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunTextEditJournalTest();
  return 0;
}
