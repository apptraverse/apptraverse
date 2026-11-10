#include "minimal_app_persistence.h"

#include <vector>

#include "minimal_application.h"

#include "minimal_app_prepare.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/journal_retention_policy.h"
#include "apptraverse/node.h"
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

void NormalizeMinimalApplicationAfterEmbeddedUserLoad(
    MinimalApplication& application) {
#if defined(APPTRAVERSE_ENABLE_DISTILLATION)
  // Embedded USER save finalizes journals (clears base). Reload leaves nodes
  // materialized but without base until runtime nodes are initialized.
  FinalizeDistilledGraph(application);
#else
  (void)application;
#endif
}

bool BootstrapMinimalApplicationProfileIfAbsent(
    std::filesystem::path const& state_dir,
    MinimalApplicationGraphBuilder const& build_graph) {
#if defined(APPTRAVERSE_ENABLE_DISTILLATION)
  std::error_code profile_error;
  bool const profile_exists = std::filesystem::exists(state_dir, profile_error);
  if (profile_error) {
    WriteFatalStderr("fatal: cannot inspect minimal app profile\n");
    std::abort();
  }
  bool const profile_absent =
      !profile_exists || std::filesystem::is_empty(state_dir, profile_error);
  if (profile_error) {
    WriteFatalStderr("fatal: cannot inspect minimal app profile contents\n");
    std::abort();
  }
  if (!profile_absent) {
    return false;
  }
  DirectoryDomainStorage bootstrap_storage{state_dir};
  ae::Domain bootstrap_domain{bootstrap_storage};
  auto bootstrap_app = build_graph(bootstrap_domain);
  PrepareMinimalApplicationForRun(*bootstrap_app);
  FinalizeDistilledGraph(*bootstrap_app);
  SaveDistilledRoot(*bootstrap_app);
  return true;
#else
  (void)state_dir;
  (void)build_graph;
  WriteFatalStderr("fatal: minimal app bootstrap requires distillation\n");
  std::abort();
#endif
}

int PersistMinimalApplication(MinimalApplication& application,
                              DirectoryDomainStorage& storage) {
  storage.BeginSaveSession();
  ui::SavePresentedWindow(application.main_window);
  if (application.auxiliary_presented_open) {
    application.auxiliary.window.Load();
    if (application.auxiliary.window.is_valid() &&
        application.auxiliary.window.is_loaded()) {
      ui::SavePresentedWindow(application.auxiliary.window);
    }
  }
  ae::DomainGraph graph{application.domain};
  MinimalApplication::ptr const root =
      MinimalApplication::ptr::MakeFromThis(&application);
  ae::seri::SeriResult const save_root =
      graph.SaveRoot(root.cached(), MinimalApplicationRootId());
  if (save_root.IsErr()) {
    WriteFatalStderr("fatal: MinimalApplication SaveRoot failed\n");
    return 1;
  }
  if (storage.SaveSessionHadFailure()) {
    WriteFatalStderr("fatal: MinimalApplication save failed\n");
    return 1;
  }
  return 0;
}

int SaveMinimalApplicationEmbeddedUserState(MinimalApplication& application) {
  FinalizeMinimalApplicationNodesForEmbeddedUserOverlay(application);
  ui::SavePresentedWindow(application.main_window);
  if (application.auxiliary_presented_open) {
    application.auxiliary.window.Load();
    if (application.auxiliary.window.is_valid() &&
        application.auxiliary.window.is_loaded()) {
      ui::SavePresentedWindow(application.auxiliary.window);
    }
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

int PersistMinimalApplicationEmbeddedUserOverlay(
    MinimalApplication& application, DirectoryDomainStorage& storage) {
  storage.BeginSaveSession();
  int const result = SaveMinimalApplicationEmbeddedUserState(application);
  if (storage.SaveSessionHadFailure()) {
    WriteFatalStderr("fatal: MinimalApplication embedded save failed\n");
    return 1;
  }
  return result;
}

int PersistMinimalApplicationEmbeddedUserOverlayFromDomain(
    ae::Domain& domain, DirectoryDomainStorage& user_storage) {
  ae::Ptr<ae::Obj> const held = domain.Find(MinimalApplicationRootId());
  auto& application = static_cast<MinimalApplication&>(*held);
  return PersistMinimalApplicationEmbeddedUserOverlay(application,
                                                      user_storage);
}

}  // namespace apptraverse::example::minimal
