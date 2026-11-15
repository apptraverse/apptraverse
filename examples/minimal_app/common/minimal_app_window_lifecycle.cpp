#include "minimal_app_window_lifecycle.h"

#include "minimal_application.h"
#include "minimal_app_window_events.h"

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/node.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"

namespace apptraverse::example::minimal {

void SealAuxiliaryWindowClosed(MinimalApplication& application) {
  application.auxiliary_presented_open = false;
  if (application.auxiliary.window.is_loaded()) {
    ui::UnloadPresentedWindow(application.auxiliary.window);
    application.auxiliary.closer.Save();
  }
  application.auxiliary.apply_button.Reset();
  application.auxiliary.row_list.Reset();
  application.auxiliary.presenters.apply_button.Reset();
  application.auxiliary.presenters.row_list.Reset();
  application.auxiliary.presenters.closer.Reset();
  application.auxiliary.closer.Reset();
}

void MinimalApplication::LoadAuxiliaryPresentation() {
  ui::LoadPresentedWindow(auxiliary.window);
  auxiliary.apply_button.Load();
  auxiliary.row_list.Load();
  auxiliary.closer.Load();
  auxiliary.presenters.apply_button.Load();
  auxiliary.presenters.row_list.Load();
  auxiliary.presenters.closer.Load();
}

void MinimalApplication::CloseAuxiliaryWindowImpl() {
  if (!auxiliary_presented_open) {
    return;
  }
  SealAuxiliaryWindowClosed(*this);
  NoteMaterializedChange();
}

void MinimalApplication::OpenAuxiliaryWindowImpl() {
  if (auxiliary_presented_open) {
    return;
  }
  LoadAuxiliaryPresentation();
  // Lazy-loaded Nodes were absent when ApplicationModelRuntime subscribed the
  // initial graph. Inherit its notifier and compaction policy before input.
  std::vector<Node*> nodes;
  CollectLiveReachableNodes(*auxiliary.window, nodes);
  std::vector<ae::Ptr<ae::Obj>> keepalive;
  keepalive.reserve(nodes.size());
  for (Node* node : nodes) {
    keepalive.push_back(domain->Find(node->obj_id));
  }
  for (Node* node : nodes) {
    node->CopyMaterializedChangeNotifierFrom(*this);
  }
  auxiliary_presented_open = true;
  LayoutHostedWindows();
  NoteMaterializedChange();
}

void MinimalApplication::NotifyAuxiliaryCloseFromUi(
    std::uint32_t window_obj_id) {
  if (!auxiliary_presented_open || auxiliary.window.id().id() != window_obj_id) {
    return;
  }
  auto event = MinimalCloseAuxiliaryWindowEvent::ptr::Create(
      ae::CreateWith{*domain});
  Commit(std::move(event));
}

}  // namespace apptraverse::example::minimal
