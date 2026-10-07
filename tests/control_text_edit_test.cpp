#include <cstdlib>
#include <iostream>

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
  } while (false)

}  // namespace

void RunControlTextEditTest() {
  EnsureTextEditControlRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto edit = TextEdit::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*edit);

  edit->RequestInsert(u"hello");
  CHECK(edit->text == u"hello");
  CHECK(edit->caret == 5);

  edit->RequestSetSelection(1, 4);
  CHECK(edit->HasSelection());
  edit->RequestReplaceRange(1, 4, u"i");
  CHECK(edit->text == u"hio");
  CHECK(edit->caret == 2);

  edit->RequestInsert(u"\U0001F600");
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
}

}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunControlTextEditTest();
  return 0;
}
