#include "minimal_app_prepare.h"

#include "minimal_application.h"

#include "minimal_app_window_lifecycle.h"

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"

namespace apptraverse::example::minimal {
namespace {

void MaterializeLabelTree(ui::UiLocalizationString::ptr const& label) {
  if (!label.is_valid()) {
    return;
  }
  label.Load();
  if (!label) {
    return;
  }
  for (auto& entry : label->strings) {
    if (entry.is_valid()) {
      entry.Load();
    }
  }
}

void MaterializeListRowLabels(MinimalAppList& list) {
  for (auto const& row : list.rows) {
    if (!row) {
      continue;
    }
    MaterializeLabelTree(row->primary_text);
  }
}

}  // namespace

void PrepareMinimalApplicationForRun(MinimalApplication& application) {
  ui::LoadPresentedWindow(application.main_window);
  application.apply_button.Load();
  application.row_list.Load();
  application.presenters.apply_button.Load();
  application.presenters.row_list.Load();
  application.presenters.auxiliary_opener.Load();
  application.auxiliary_opener.Load();
  if (!application.apply_button || !application.row_list ||
      !application.presenters.apply_button ||
      !application.presenters.row_list ||
      !application.presenters.auxiliary_opener || !application.auxiliary_opener) {
    WriteFatalStderr(
        "fatal: MinimalApplication required control or presenter missing\n");
    std::abort();
  }
  if (!application.apply_button->label) {
    WriteFatalStderr("fatal: MinimalApplication apply_button missing label\n");
    std::abort();
  }
  if (!application.auxiliary_opener->label.is_valid()) {
    application.auxiliary_opener->label =
        ui::UiLocalizationString::ptr::Declare(
            ae::CreateWith{*application.auxiliary_opener->domain}.with_id(
                MinimalAuxiliaryOpenerLabelId()));
  }
  application.auxiliary_opener->label.Load();
  if (!application.auxiliary_opener->label) {
    WriteFatalStderr(
        "fatal: MinimalApplication auxiliary_opener missing label\n");
    std::abort();
  }
  if (!application.apply_button->window_title ||
      !application.row_list->window_title) {
    WriteFatalStderr(
        "fatal: MinimalApplication missing window_title wiring\n");
    std::abort();
  }
  for (auto const& row : application.row_list->rows) {
    if (!row || !row->primary_text) {
      WriteFatalStderr(
          "fatal: MinimalApplication list row missing primary_text\n");
      std::abort();
    }
  }
  auto const& window_presenter =
      application.presenters.apply_button->window_presenter;
  if (!window_presenter || !window_presenter->locale) {
    WriteFatalStderr(
        "fatal: MinimalApplication window presenter missing locale\n");
    std::abort();
  }

  MaterializeLabelTree(application.apply_button->label);
  MaterializeLabelTree(application.auxiliary_opener->label);
  MaterializeListRowLabels(*application.row_list);
  if (application.auxiliary_presented_open) {
    MaterializeLabelTree(application.auxiliary.apply_button->label);
    MaterializeListRowLabels(*application.auxiliary.row_list);
    MaterializeLabelTree(application.auxiliary.closer->label);
  }

#if !defined(__EMSCRIPTEN__)
  if (application.auxiliary_presented_open) {
    application.LoadAuxiliaryPresentation();
  } else {
    SealAuxiliaryWindowClosed(application);
  }
#endif
}

}  // namespace apptraverse::example::minimal
