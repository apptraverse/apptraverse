#include <cstdlib>
#include <iostream>
#include <string>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj.h"

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

}  // namespace

void RunControlTextEditTest() {
  EnsureTextEditControlRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto edit = TextEdit::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*edit);

  edit->RequestInsert("hello");
  CHECK(edit->text == "hello");
  CHECK(edit->caret == 5);

  edit->RequestSetSelection(1, 4);
  CHECK(edit->HasSelection());
  edit->RequestReplaceRange(1, 4, "i");
  CHECK(edit->text == "hio");
  CHECK(edit->caret == 2);

  edit->RequestInsert("\xF0\x9F\x98\x80");
  CHECK(edit->text.size() >= 5);

  ByteSink snapshot;
  SerializePersistentModelSnapshot(*edit, snapshot);

  ae::RamDomainStorage load_storage;
  ae::Domain load_domain{load_storage};
  auto load_edit =
      TextEdit::ptr::Create(ae::CreateWith{load_domain}.with_id(edit->obj_id));
  InitializeRuntimeNode(*load_edit);
  ByteSource in;
  in.data = snapshot.bytes.data();
  in.size = snapshot.bytes.size();
  in.pos = 0;
  LoadPersistentModelSnapshot(in, load_domain, load_storage, *load_edit);
  CHECK(load_edit->text == edit->text);
  CHECK(load_edit->caret == edit->caret);
  CHECK(load_edit->selection_anchor == edit->selection_anchor);

  edit->RequestSetSelection(1, 4);
  CHECK(edit->HasSelection());
  ByteSink caret_snapshot;
  SerializePersistentModelSnapshot(*edit, caret_snapshot);
  ae::RamDomainStorage caret_storage;
  ae::Domain caret_domain{caret_storage};
  auto caret_edit = TextEdit::ptr::Create(
      ae::CreateWith{caret_domain}.with_id(edit->obj_id));
  InitializeRuntimeNode(*caret_edit);
  ByteSource caret_in;
  caret_in.data = caret_snapshot.bytes.data();
  caret_in.size = caret_snapshot.bytes.size();
  caret_in.pos = 0;
  LoadPersistentModelSnapshot(caret_in, caret_domain, caret_storage, *caret_edit);
  CHECK(caret_edit->text == edit->text);
  CHECK(caret_edit->caret == edit->caret);
  CHECK(caret_edit->selection_anchor == edit->selection_anchor);

  auto after_event =
      TextEditInsertEvent::ptr::Create(ae::CreateWith{caret_domain});
  after_event->insert_text = "!";
  caret_edit->Commit(after_event);
  CHECK(caret_edit->text.find('!') != std::string::npos);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunControlTextEditTest();
  return 0;
}
