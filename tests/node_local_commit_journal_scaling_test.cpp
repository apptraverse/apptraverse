#include <chrono>
#include <cstdlib>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_window.h"
#include "apptraverse/ui/ui_presenters.h"

using namespace apptraverse;
using namespace apptraverse::ui;

namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (false)

void SeedJournal(UiWindow& edit, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) {
    auto event = SetUiWindowClientSizeEvent::ptr::Create(ae::CreateWith{*edit.domain});
    event->width = static_cast<std::int32_t>(i + 1);
    event->height = 600;
    edit.Commit(event);
  }
}

std::chrono::nanoseconds TimeOneLocalCommit(UiWindow& edit) {
  auto event = SetUiWindowClientSizeEvent::ptr::Create(ae::CreateWith{*edit.domain});
  event->width = 1200;
  event->height = 600;
  auto const start = std::chrono::steady_clock::now();
  edit.Commit(std::move(event));
  return std::chrono::steady_clock::now() - start;
}

void TestLocalCommitScalesWithJournalSize() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto edit = UiWindow::ptr::Create(ae::CreateWith{domain});
  edit->BootstrapClientSize(800, 600);
  InitializeRuntimeNode(*edit);

  SeedJournal(*edit, 100);
  auto const t100 = TimeOneLocalCommit(*edit);

  SeedJournal(*edit, 900);
  CHECK(edit->journal.size() >= 1000);
  auto const t1000 = TimeOneLocalCommit(*edit);

  SeedJournal(*edit, 9000);
  CHECK(edit->journal.size() >= 10000);
  auto const t10000 = TimeOneLocalCommit(*edit);

  CHECK(t10000 <= t1000 * 4 + std::chrono::milliseconds(5));
  CHECK(t1000 <= t100 * 4 + std::chrono::milliseconds(5));

  std::cout << "local commit ns: 100=" << t100.count() << " 1000=" << t1000.count()
            << " 10000=" << t10000.count() << '\n';
}

}  // namespace

int main() {
  EnableNoninteractiveCrt();
  EnsureObjectRegistration();
  EnsureUiPresenterRegistration();
  TestLocalCommitScalesWithJournalSize();
  std::cout << "node_local_commit_journal_scaling_test OK\n";
  return 0;
}
