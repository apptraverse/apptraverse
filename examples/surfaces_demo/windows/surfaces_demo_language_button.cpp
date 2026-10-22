#include "surfaces_demo_language_button.h"

#include <cstdlib>

#include "apptraverse/surfaces/surfaces_ids.h"
#include "apptraverse/surfaces/surfaces_model.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::example::surfaces {

void SurfacesDemoLanguageButton::HandlePushButtonClicked() {
  if (!locale || domain == nullptr) {
    std::abort();
  }
  auto application_obj = domain->Find(ae::ObjId{surfaces_demo::ToObjId(
      surfaces_demo::ObjId::Application)});
  if (!application_obj) {
    std::abort();
  }
  Application& application = static_cast<Application&>(*application_obj);
  if (!application.label_add || !application.label_close ||
      !application.window_title_prefix || !application.language_switch_label) {
    std::abort();
  }

  int const next = 1 - locale->language_index();

  application.label_add->MaterializeLanguage(next);
  application.label_close->MaterializeLanguage(next);
  application.window_title_prefix->MaterializeLanguage(next);
  application.language_switch_label->MaterializeLanguage(next);
  locale->SubmitLanguageIndexFromUi(next);
}

}  // namespace apptraverse::example::surfaces
