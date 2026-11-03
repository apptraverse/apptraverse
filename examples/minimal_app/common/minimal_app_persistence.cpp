#include "minimal_app_persistence.h"

#include <unordered_set>
#include <vector>

#include "minimal_application.h"

#include "minimal_app_prepare.h"
#include "minimal_app_window_lifecycle.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/graph_walk.h"
#include "apptraverse/journal_retention_policy.h"
#include "apptraverse/node.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"

namespace apptraverse::example::minimal {
namespace {

void CollectLiveReachableNodesFromAuxiliaryFields(
    MinimalApplication& application, std::vector<Node*>& out) {
  std::unordered_set<std::uint32_t> seen;
  std::vector<ae::Obj*> stack;
  auto push_ref = [&](auto const& ref) {
    if (!ref.is_valid()) {
      return;
    }
    if (!seen.insert(ref.id().id()).second) {
      return;
    }
    if (ae::Ptr<ae::Obj> obj = application.domain->Find(ref.id())) {
      stack.push_back(&*obj);
    }
  };

  push_ref(application.auxiliary.window);
  push_ref(application.auxiliary.apply_button);
  push_ref(application.auxiliary.row_list);
  push_ref(application.auxiliary.closer);
  push_ref(application.auxiliary.presenters.apply_button);
  push_ref(application.auxiliary.presenters.row_list);
  push_ref(application.auxiliary.presenters.closer);

  out.clear();
  while (!stack.empty()) {
    ae::Obj* obj = stack.back();
    stack.pop_back();
    if (obj->GetClassId() == Node::kClassId) {
      out.push_back(static_cast<Node*>(obj));
    }
    ForEachGraphEdgeObjPtrOn(*obj, [&](auto const& edge) { push_ref(edge); });
  }
}

void CollectMinimalApplicationEmbeddedFinalizeNodes(
    MinimalApplication& application, std::vector<Node*>& out) {
  CollectLiveReachableNodes(application, out);
  if (application.auxiliary_presented_open) {
    return;
  }
  std::vector<Node*> aux_nodes;
  CollectLiveReachableNodesFromAuxiliaryFields(application, aux_nodes);
  std::unordered_set<std::uint32_t> aux_ids;
  aux_ids.reserve(aux_nodes.size());
  for (Node* node : aux_nodes) {
    aux_ids.insert(node->obj_id.id());
  }
  std::vector<Node*> filtered;
  filtered.reserve(out.size());
  for (Node* node : out) {
    if (aux_ids.find(node->obj_id.id()) == aux_ids.end()) {
      filtered.push_back(node);
    }
  }
  out = std::move(filtered);
}

void SaveSealedAuxiliarySubgraphForPersist(MinimalApplication& application) {
  if (application.auxiliary_presented_open ||
      !application.auxiliary_subgraph_materialized_in_session) {
    return;
  }
  EnsureMinimalApplicationAuxiliaryLazyRefs(application);
  auto save_if_present = [](auto& ref) {
    if (!ref.is_valid()) {
      return;
    }
    ref.Load();
    if (ref) {
      ref.Save();
    }
  };
  save_if_present(application.auxiliary.apply_button);
  save_if_present(application.auxiliary.row_list);
  save_if_present(application.auxiliary.closer);
  save_if_present(application.auxiliary.presenters.apply_button);
  save_if_present(application.auxiliary.presenters.row_list);
  save_if_present(application.auxiliary.presenters.closer);
  save_if_present(application.auxiliary.window);
  if (application.auxiliary.window.is_valid()) {
    application.auxiliary.window.Load();
    if (application.auxiliary.window &&
        application.auxiliary.window->presenter.is_valid()) {
      application.auxiliary.window->presenter.Load();
      if (application.auxiliary.window->presenter) {
        application.auxiliary.window->presenter.Save();
      }
    }
  }
}

}  // namespace

void FinalizeMinimalApplicationNodesForEmbeddedUserOverlay(
    MinimalApplication& application) {
  std::vector<Node*> nodes;
  CollectMinimalApplicationEmbeddedFinalizeNodes(application, nodes);
  std::uint64_t const now_us = SystemUtcMicros();
  JournalRetentionPolicy const persist_retention{.max_events = 0};
  for (Node* node : nodes) {
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
  if (application.auxiliary_subgraph_materialized_in_session) {
    EnsureMinimalApplicationAuxiliaryLazyRefs(application);
    SaveSealedAuxiliarySubgraphForPersist(application);
  }
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

int PersistMinimalApplicationEmbeddedUserOverlay(
    MinimalApplication& application, DirectoryDomainStorage& storage) {
  storage.BeginSaveSession();
  if (application.auxiliary_subgraph_materialized_in_session) {
    EnsureMinimalApplicationAuxiliaryLazyRefs(application);
    SaveSealedAuxiliarySubgraphForPersist(application);
  }
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
  if (storage.SaveSessionHadFailure()) {
    WriteFatalStderr("fatal: MinimalApplication embedded save failed\n");
    return 1;
  }
  return 0;
}

int PersistMinimalApplicationFromDomain(ae::Domain& domain,
                                        std::filesystem::path const& state_dir) {
  ae::Ptr<ae::Obj> const held = domain.Find(MinimalApplicationRootId());
  if (!held) {
    WriteFatalStderr("fatal: MinimalApplication missing from model domain\n");
    return 1;
  }
  auto& application = static_cast<MinimalApplication&>(*held);
  DirectoryDomainStorage storage{state_dir};
  return PersistMinimalApplication(application, storage);
}

}  // namespace apptraverse::example::minimal
