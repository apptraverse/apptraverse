#pragma once
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_window_presenter.h"
#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_string.h"

namespace apptraverse::test {
// Explicit distilled fixture for tests whose subject is not window text.
inline void BuildEmptyWindowTitle(ui::UiWindowPresenter& presenter) {
  auto& domain = *presenter.domain;
  presenter.locale = ui::UiLocaleSettings::ptr::Create(ae::CreateWith{domain});
  presenter.locale->BootstrapLanguageIndex(0);
  InitializeRuntimeNode(*presenter.locale);
  presenter.title = ui::UiLocalizationString::ptr::Create(ae::CreateWith{domain});
  for (int language = 0; language != 2; ++language) {
    auto text = ui::UiString::ptr::Create(ae::CreateWith{domain});
    text->utf8 = "";
    text.Save();
    presenter.title->strings.push_back(text);
  }
  InitializeRuntimeNode(*presenter.title);
  presenter.title->MaterializeLanguage(0);
}
}
