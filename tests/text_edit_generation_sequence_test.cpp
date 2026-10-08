#include <cstdlib>
#include <iostream>
#include <string>

#include "aether-objects/domain_storage/ram_domain_storage.h"

#include "apptraverse/control_text_edit.h"
#include "apptraverse/node.h"
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

struct NotifierTrace {
  int call_count{0};

  static void Notify(void* ctx, Node&) {
    ++static_cast<NotifierTrace*>(ctx)->call_count;
  }
};

void ExpectEvent(TextEdit& edit, NotifierTrace& trace, std::uint64_t expected_generation,
                 std::size_t expected_journal) {
  CHECK(edit.Generation() == expected_generation);
  CHECK(edit.journal.size() == expected_journal);
  CHECK(trace.call_count == static_cast<int>(expected_journal));
}

void RunTextEditGenerationSequenceTest() {
  EnsureTextEditControlRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto edit = TextEdit::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*edit);
  NotifierTrace trace;
  edit->BindMaterializedChangeNotifier(&trace, &NotifierTrace::Notify);

  std::uint64_t gen = edit->Generation();
  std::size_t journal = 0;

  edit->RequestInsert("hello");
  ++journal;
  ++gen;
  ExpectEvent(*edit, trace, gen, journal);
  CHECK(edit->text == "hello");

  edit->RequestSetCaret(2);
  ++journal;
  ++gen;
  ExpectEvent(*edit, trace, gen, journal);
  CHECK(edit->caret == 2);

  edit->RequestSetSelection(1, 4);
  ++journal;
  ++gen;
  ExpectEvent(*edit, trace, gen, journal);
  CHECK(edit->HasSelection());

  edit->RequestReplaceRange(1, 4, "i");
  ++journal;
  ++gen;
  ExpectEvent(*edit, trace, gen, journal);
  CHECK(edit->text == "hio");

  edit->RequestDeleteForward(1);
  ++journal;
  ++gen;
  ExpectEvent(*edit, trace, gen, journal);

  edit->RequestDeleteBackward(1);
  ++journal;
  ++gen;
  ExpectEvent(*edit, trace, gen, journal);

  edit->RequestSetSelection(0, static_cast<std::uint32_t>(edit->text.size()));
  ++journal;
  ++gen;
  ExpectEvent(*edit, trace, gen, journal);
  edit->RequestDeleteSelection();
  ++journal;
  ++gen;
  ExpectEvent(*edit, trace, gen, journal);

  edit->RequestClear();
  ++journal;
  ++gen;
  ExpectEvent(*edit, trace, gen, journal);
  CHECK(edit->text.empty());

  edit->RequestInsert("\xD0\x9F\xD1\x80");
  ++journal;
  ++gen;
  ExpectEvent(*edit, trace, gen, journal);

  edit->RequestInsert("A\xF0\x9F\x98\x80""B");
  ++journal;
  ++gen;
  ExpectEvent(*edit, trace, gen, journal);

  std::uint64_t const caret_at = edit->caret;
  edit->RequestSetCaret(caret_at);
  ++journal;
  ++gen;
  ExpectEvent(*edit, trace, gen, journal);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunTextEditGenerationSequenceTest();
  std::cerr << "text_edit_generation_sequence_test OK\n";
  return 0;
}
