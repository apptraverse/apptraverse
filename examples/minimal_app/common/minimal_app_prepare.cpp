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
}

}  // namespace apptraverse::example::minimal
