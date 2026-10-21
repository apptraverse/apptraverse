#include "apptraverse/surfaces/surfaces_localization.h"

#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_string.h"

namespace apptraverse {
namespace {

ui::UiLocalizationString::ptr MakeLocalizedString(
    ae::Domain& domain, Surfaces& surfaces, char const* en, char const* ru) {
  auto localized = ui::UiLocalizationString::ptr::Create(
      ae::CreateWith{domain});
  InitializeRuntimeNode(*localized, surfaces);
  auto en_string = ui::UiString::ptr::Create(ae::CreateWith{domain});
  en_string->utf8 = en;
  auto ru_string = ui::UiString::ptr::Create(ae::CreateWith{domain});
  ru_string->utf8 = ru;
  localized->strings = {en_string, ru_string};
  return localized;
}

}  // namespace

void InstallSurfacesDemoLocalization(Application& application,
                                     ae::Domain& domain) {
  Surfaces& surfaces = *application.surfaces;
  application.locale = ui::UiLocaleSettings::ptr::Create(
      ae::CreateWith{domain});
  InitializeRuntimeNode(*application.locale, surfaces);

  application.label_add =
      MakeLocalizedString(domain, surfaces, "Add", "Добавить");
  application.label_close = MakeLocalizedString(
      domain, surfaces, "Close this window", "Закрыть это окно");
  application.window_title_prefix =
      MakeLocalizedString(domain, surfaces, "Surface ", "Поверхность ");
  application.language_switch_label =
      MakeLocalizedString(domain, surfaces, "RU", "EN");
}

std::string FormatSurfaceWindowTitle(ui::UiLocalizationString const& prefix,
                                     int language_index,
                                     std::uint32_t number) {
  return prefix.Resolve(language_index) + std::to_string(number);
}

#if APPTRAVERSE_SURFACE_DEMO_REVISION == 1
std::string FormatSurfaceWindowTitleWithSuffix(
    ui::UiLocalizationString const& prefix, int language_index,
    Surface const& surface) {
  std::string title =
      FormatSurfaceWindowTitle(prefix, language_index, surface.number);
  title += surface.window_title_suffix;
  return title;
}
#endif

}  // namespace apptraverse
