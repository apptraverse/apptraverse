#include <cstdlib>
#include <iostream>

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
  ae::ObjId last_obj_id{};

  static void Notify(void* ctx, Node& node) {
    auto* self = static_cast<NotifierTrace*>(ctx);
    ++self->call_count;
    self->last_obj_id = node.obj_id;
  }
};

void RunTextEditPublicationGenerationTest() {
  EnsureTextEditControlRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto edit = TextEdit::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*edit);

  std::uint64_t const generation0 = edit->Generation();
  NotifierTrace trace;
  PendingDirtyNodes pending;
  edit->BindMaterializedChangeNotifier(&trace, &NotifierTrace::Notify);

  edit->RequestInsert("A");
  CHECK(edit->text == "A");
  CHECK(edit->journal.size() == 1U);
  CHECK(edit->Generation() == generation0 + 1);
  CHECK(trace.call_count == 1);
  CHECK(trace.last_obj_id == edit->obj_id);

  edit->BindMaterializedChangeNotifier(&pending, &PendingDirtyNodesNotify);
  edit->RequestInsert("B");
  CHECK(edit->Generation() == generation0 + 2);
  CHECK(pending.ordered.size() == 1U);
  CHECK(pending.ordered.front() == edit->obj_id.id());
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunTextEditPublicationGenerationTest();
  std::cerr << "text_edit_publication_generation_test OK\n";
  return 0;
}
