#include "minimal_app_window_lifecycle.h"

#include "minimal_application.h"

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"

namespace apptraverse::example::minimal {

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
  MinimalApplication::ptr::MakeFromThis(this).Save();
  NoteMaterializedChange();
}

void MinimalApplication::OpenAuxiliaryWindowImpl() {
  if (auxiliary_presented_open) {
    return;
  }
  LoadAuxiliaryPresentation();
  auxiliary_presented_open = true;
  MinimalApplication::ptr::MakeFromThis(this).Save();
  NoteMaterializedChange();
}

}  // namespace apptraverse::example::minimal
