#include "minimal_app_persistence.h"

#include <vector>

#include "minimal_application.h"


#include "apptraverse/journal_retention_policy.h"
#include "apptraverse/node.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"

namespace apptraverse::example::minimal {

void FinalizeMinimalApplicationNodesForEmbeddedUserOverlay(
    MinimalApplication& application) {
  std::vector<ae::Ptr<ae::Obj>> nodes;
  CollectReachableNodeRefs(application, nodes);
  std::uint64_t const now_us = SystemUtcMicros();
  JournalRetentionPolicy const persist_retention{.max_events = 0};
  for (ae::Ptr<ae::Obj> const& held : nodes) {
    auto* node = static_cast<Node*>(&*held);
    if (node->journal.empty()) {
      continue;
    }
    JournalRetentionPolicy const saved_retention =
        node->GetJournalRetentionPolicy();
    node->SetJournalRetentionPolicy(persist_retention);
    node->CompactJournal(now_us);
    node->SetJournalRetentionPolicy(saved_retention);
    if (!node->journal.empty()) {
      WriteFatalStderr(
          "fatal: embedded USER overlay save requires compacted journal\n");
      std::abort();
    }
  }
}

int SaveMinimalApplicationEmbeddedUserState(MinimalApplication& application) {
  FinalizeMinimalApplicationNodesForEmbeddedUserOverlay(application);
  ui::SavePresentedWindow(application.main_window);
  if (application.auxiliary_presented_open) {
    application.auxiliary.window.Load();
    ui::SavePresentedWindow(application.auxiliary.window);
  }
  ae::DomainGraph graph{application.domain};
  MinimalApplication::ptr const root =
      MinimalApplication::ptr::MakeFromThis(&application);
  ae::seri::SeriResult const save_root =
      graph.SaveRoot(root.cached(), MinimalApplicationRootId());
  if (save_root.IsErr()) {
    WriteFatalStderr("fatal: MinimalApplication embedded SaveRoot failed\n");
    return 1;
  }
  return 0;
}

}  // namespace apptraverse::example::minimal
