#include "minimal_app_prepare.h"

#include "minimal_application.h"

#include "minimal_app_window_lifecycle.h"

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

  int const language_index = ActiveLanguageIndex(
      *application.presenters.apply_button->window_presenter);
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
