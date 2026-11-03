#include "minimal_app_window_lifecycle.h"

#include "minimal_application.h"
#include "minimal_app_controls.h"

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::example::minimal {

void EnsureMinimalApplicationAuxiliaryLazyRefs(
    MinimalApplication& application) {
  ae::Domain& domain = *application.domain;
  if (!application.auxiliary.window.is_valid()) {
    application.auxiliary.window = ui::UiDesktopWindow::ptr::Declare(
        ae::CreateWith{domain}.with_id(MinimalAuxiliaryWindowId()));
  }
  if (!application.auxiliary.apply_button.is_valid()) {
    application.auxiliary.apply_button = MinimalApplyButton::ptr::Declare(
        ae::CreateWith{domain}.with_id(MinimalAuxiliaryApplyButtonId()));
  }
  if (!application.auxiliary.row_list.is_valid()) {
    application.auxiliary.row_list = MinimalAppList::ptr::Declare(
        ae::CreateWith{domain}.with_id(MinimalAuxiliaryRowListId()));
  }
  if (!application.auxiliary.closer.is_valid()) {
    application.auxiliary.closer = MinimalAuxiliaryCloserButton::ptr::Declare(
        ae::CreateWith{domain}.with_id(MinimalAuxiliaryCloserId()));
  }
  if (!application.auxiliary.presenters.apply_button.is_valid()) {
    application.auxiliary.presenters.apply_button =
        ui::UiPushButtonPresenter::ptr::Declare(
            ae::CreateWith{domain}.with_id(
                MinimalAuxiliaryApplyButtonPresenterId()));
  }
  if (!application.auxiliary.presenters.row_list.is_valid()) {
    application.auxiliary.presenters.row_list =
        ui::UiListContainerPresenter::ptr::Declare(
            ae::CreateWith{domain}.with_id(
                MinimalAuxiliaryRowListPresenterId()));
  }
  if (!application.auxiliary.presenters.closer.is_valid()) {
    application.auxiliary.presenters.closer =
        ui::UiPushButtonPresenter::ptr::Declare(
            ae::CreateWith{domain}.with_id(
                MinimalAuxiliaryCloserPresenterId()));
  }
}

void SealAuxiliaryWindowClosed(MinimalApplication& application) {
  application.auxiliary_presented_open = false;
  if (application.auxiliary.window.is_valid() &&
      application.auxiliary.window.is_loaded()) {
    ui::UnloadPresentedWindow(application.auxiliary.window);
  }
  application.auxiliary.apply_button.Reset();
  application.auxiliary.row_list.Reset();
  application.auxiliary.presenters.apply_button.Reset();
  application.auxiliary.presenters.row_list.Reset();
  application.auxiliary.presenters.closer.Reset();
  application.auxiliary.closer.Reset();
}

void MinimalApplication::LoadAuxiliaryPresentation() {
  EnsureMinimalApplicationAuxiliaryLazyRefs(*this);
  ui::LoadPresentedWindow(auxiliary.window);
  auxiliary.apply_button.Load();
  auxiliary.row_list.Load();
  auxiliary.closer.Load();
  auxiliary.presenters.apply_button.Load();
  auxiliary.presenters.row_list.Load();
  auxiliary.presenters.closer.Load();
  if (!auxiliary.apply_button || !auxiliary.row_list || !auxiliary.closer ||
      !auxiliary.presenters.apply_button ||
      !auxiliary.presenters.row_list || !auxiliary.presenters.closer) {
    WriteFatalStderr(
        "fatal: MinimalApplication auxiliary control or presenter missing\n");
    std::abort();
  }
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
  auxiliary_presented_open = true;
  auxiliary_subgraph_materialized_in_session = true;
  NoteMaterializedChange();
}

}  // namespace apptraverse::example::minimal
