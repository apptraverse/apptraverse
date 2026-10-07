#include <cstdlib>
#include <iostream>
#include <string>

#include "aether-objects/domain_storage/ram_domain_storage.h"

#include "apptraverse/control_text_edit.h"
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

bool IsValidBoundary(std::u16string const& text, std::uint32_t index) {
  if (index >= text.size()) {
    return true;
  }
  if (index == 0) {
    return true;
  }
  char16_t const prev = text[index - 1];
  char16_t const cur = text[index];
  if (prev >= 0xD800 && prev <= 0xDBFF && cur >= 0xDC00 && cur <= 0xDFFF) {
    return false;
  }
  return true;
}

void CheckAllBoundaries(TextEdit const& edit) {
  CHECK(IsValidBoundary(edit.text, edit.caret));
  CHECK(IsValidBoundary(edit.text, edit.selection_anchor));
}

}  // namespace

void RunTextEditUtf16BoundaryTest() {
  EnsureTextEditControlRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto edit = TextEdit::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*edit);

  edit->RequestInsert(u"hello");
  edit->RequestInsert(u"\u043F\u0440\u0438\u0432\u0435\u0442");
  edit->RequestInsert(u"\U0001F600");
  CheckAllBoundaries(*edit);

  std::uint32_t const emoji_start =
      static_cast<std::uint32_t>(edit->text.size()) - 2;
  edit->RequestSetCaret(emoji_start + 1);
  CheckAllBoundaries(*edit);
  CHECK(edit->caret == emoji_start + 2);

  edit->RequestSetSelection(emoji_start + 1, emoji_start + 1);
  CheckAllBoundaries(*edit);
  edit->RequestReplaceRange(emoji_start + 1, emoji_start + 1, u"!");
  CheckAllBoundaries(*edit);

  edit->RequestSetSelection(0, edit->text.size());
  edit->RequestDeleteSelection();
  CheckAllBoundaries(*edit);

  edit->RequestInsert(u"A\U0001F600B");
  edit->RequestSetCaret(2);
  CheckAllBoundaries(*edit);
  edit->RequestDeleteBackward(1);
  CheckAllBoundaries(*edit);
  edit->RequestDeleteForward(1);
  CheckAllBoundaries(*edit);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunTextEditUtf16BoundaryTest();
  return 0;
}
