#include <cstdlib>
#include <iostream>
#include <string>

#include "aether-objects/domain_storage/ram_domain_storage.h"

#include "apptraverse/control_text_edit.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/utf8_text.h"

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

void CheckAllBoundaries(TextEdit const& edit) {
  CHECK(IsUtf8Boundary(edit.text, edit.caret));
  CHECK(IsUtf8Boundary(edit.text, edit.selection_anchor));
}

}  // namespace

void RunTextEditUtf8BoundaryTest() {
  EnsureTextEditControlRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto edit = TextEdit::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*edit);

  edit->RequestInsert("hello");
  edit->RequestInsert("\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
  edit->RequestInsert("\xF0\x9F\x98\x80");
  CheckAllBoundaries(*edit);

  std::string const ab_emoji_b = "A\xF0\x9F\x98\x80\x42";
  edit->RequestClear();
  edit->RequestInsert(ab_emoji_b);
  CHECK(edit->text == ab_emoji_b);
  CHECK(edit->text.size() == 6u);
  CHECK(IsUtf8Boundary(edit->text, 0));
  CHECK(IsUtf8Boundary(edit->text, 1));
  CHECK(IsUtf8Boundary(edit->text, 5));
  CHECK(IsUtf8Boundary(edit->text, 6));
  CHECK(!IsUtf8Boundary(edit->text, 2));
  CHECK(!IsUtf8Boundary(edit->text, 3));
  CHECK(!IsUtf8Boundary(edit->text, 4));

  edit->RequestSetCaret(2);
  CheckAllBoundaries(*edit);
  CHECK(edit->caret == 5);

  edit->RequestSetSelection(1, 5);
  edit->RequestReplaceRange(1, 5, "!");
  CheckAllBoundaries(*edit);
  CHECK(edit->text == "A!B");

  edit->RequestClear();
  edit->RequestInsert(ab_emoji_b);
  edit->RequestSetCaret(5);
  edit->RequestDeleteBackward(1);
  CheckAllBoundaries(*edit);
  CHECK(edit->text == "AB");

  edit->RequestClear();
  edit->RequestInsert(ab_emoji_b);
  edit->RequestSetCaret(1);
  edit->RequestDeleteForward(1);
  CheckAllBoundaries(*edit);
  CHECK(edit->text == "AB");

  std::string const combining = "e\xcc\x81";
  edit->RequestClear();
  edit->RequestInsert(combining);
  CHECK(CountUtf8CodePoints(edit->text) == 2);
  CheckAllBoundaries(*edit);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunTextEditUtf8BoundaryTest();
  return 0;
}
