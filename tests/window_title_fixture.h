#pragma once
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_window_presenter.h"
#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_string.h"
#include "apptraverse/ui/ui_presented_window.h"

namespace apptraverse::test {
// Explicit standard-component composition for native fixtures.
inline void BuildWindowPresentationServices(ui::UiPresentedWindow& window) {
  auto& domain = *window.domain;
  auto definition = ui::UiLayoutDefinition::ptr::Create(ae::CreateWith{domain});
  definition->root = ui::UiLayoutNode{};
  definition->root.flow = ui::UiLayoutFlow::kVertical;
  window.layout_state = ui::UiLayoutState::ptr::Create(ae::CreateWith{domain});
  window.layout_state->Bootstrap(definition, false, ui::UiLayoutNode{});
  InitializeRuntimeNode(*window.layout_state);
  window.device_state = ui::UiDeviceState::ptr::Create(ae::CreateWith{domain});
  window.device_state->Bootstrap({ui::UiThemeMode::kAuto, false}, ui::UiDeviceEnvironment{});
  InitializeRuntimeNode(*window.device_state);
  window.caption_close_control = {};
  window.caption_close_control_id = 0;
}
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
