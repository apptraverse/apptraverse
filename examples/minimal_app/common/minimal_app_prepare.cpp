#include "minimal_app_prepare.h"

#include "minimal_application.h"

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"

namespace apptraverse::example::minimal {

void PrepareMinimalApplicationForRun(MinimalApplication& application) {
  ui::LoadPresentedWindow(application.main_window);
  application.apply_button.Load();
  application.row_list.Load();
  application.presenters.apply_button.Load();
  application.presenters.row_list.Load();
  if (!application.apply_button || !application.row_list ||
      !application.presenters.apply_button ||
      !application.presenters.row_list) {
    WriteFatalStderr(
        "fatal: MinimalApplication required control or presenter missing\n");
    std::abort();
  }
  if (!application.apply_button->label) {
    WriteFatalStderr("fatal: MinimalApplication apply_button missing label\n");
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
}

}  // namespace apptraverse::example::minimal
