#include "minimal_app_prepare.h"

#include "minimal_application.h"

#include "minimal_app_window_lifecycle.h"

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"
#include "apptraverse/ui/ui_window_presenter.h"

namespace apptraverse::example::minimal {
namespace {

void MaterializePresentedLabel(ui::UiLocalizationString::ptr const& label,
                               int language_index) {
  label->MaterializeLanguage(language_index);
}

void MaterializeListRowLabels(MinimalAppList& list, int language_index) {
  for (auto const& row : list.rows) {
    MaterializePresentedLabel(row->primary_text, language_index);
  }
}

void MaterializeWindowPresenterTitle(ui::UiWindowPresenter& presenter,
                                     int language_index) {
  presenter.title.Load();
  MaterializePresentedLabel(presenter.title, language_index);
}

int ActiveLanguageIndex(ui::UiWindowPresenter& main_window_presenter) {
  main_window_presenter.locale.Load();
  return main_window_presenter.locale->language_index();
}

void MaterializeMainWindowLocalizedContent(MinimalApplication& application,
                                           int language_index) {
  MaterializePresentedLabel(application.apply_button->label, language_index);
  MaterializePresentedLabel(application.auxiliary_opener->label, language_index);
  MaterializeListRowLabels(*application.row_list, language_index);
  MaterializeWindowPresenterTitle(
      *application.presenters.apply_button->window_presenter, language_index);
}

void MaterializeOpenAuxiliaryLocalizedContent(MinimalApplication& application,
                                              int language_index) {
  MaterializePresentedLabel(application.auxiliary.apply_button->label,
                            language_index);
  MaterializeListRowLabels(*application.auxiliary.row_list, language_index);
  MaterializePresentedLabel(application.auxiliary.closer->label, language_index);
  application.auxiliary.window->presenter.Load();
  MaterializeWindowPresenterTitle(*application.auxiliary.window->presenter,
                                  language_index);
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
  if (!application.apply_button->label.is_valid()) {
    WriteFatalStderr(
        "fatal: MinimalApplication apply_button label ref invalid\n");
    std::abort();
  }
  application.apply_button->label.Load();
  if (!application.apply_button->label) {
    WriteFatalStderr("fatal: MinimalApplication apply_button missing label\n");
    std::abort();
  }
  if (!application.auxiliary_opener->label.is_valid()) {
    WriteFatalStderr(
        "fatal: MinimalApplication auxiliary_opener label ref invalid\n");
    std::abort();
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

  int const language_index = ActiveLanguageIndex(*window_presenter);
  MaterializeMainWindowLocalizedContent(application, language_index);

#if !defined(__EMSCRIPTEN__)
  if (application.auxiliary_presented_open) {
    application.LoadAuxiliaryPresentation();
    MaterializeOpenAuxiliaryLocalizedContent(application, language_index);
  } else {
    SealAuxiliaryWindowClosed(application);
  }
#else
  if (application.auxiliary_presented_open) {
    application.LoadAuxiliaryPresentation();
    MaterializeOpenAuxiliaryLocalizedContent(application, language_index);
  }
#endif
}

}  // namespace apptraverse::example::minimal
