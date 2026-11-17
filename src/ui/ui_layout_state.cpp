#include "apptraverse/ui/ui_layout_state.h"

#include <utility>
#include "apptraverse/noninteractive_crt.h"

namespace apptraverse::ui {

UiLayoutNode const& UiLayoutState::EffectiveRoot() const {
  if (has_override) {
    return override_root;
  }
  definition.Load();
  return definition->root;
}

bool UiLayoutState::TrySetOverride(UiLayoutNode root) {
  if (!ValidateUiConstraintLayout(root)) return false;
  auto event = SetUiLayoutOverrideEvent::ptr::Create(ae::CreateWith{*domain});
  event->root = std::move(root);
  Commit(event);
  return true;
}

void UiLayoutState::SetOverride(UiLayoutNode root) {
  if (!TrySetOverride(std::move(root))) {
    WriteFatalStderr("fatal: invalid layout constraints\n");
    std::abort();
  }
}

void UiLayoutState::ClearOverride() {
  if (!has_override) return;
  auto event = ClearUiLayoutOverrideEvent::ptr::Create(ae::CreateWith{*domain});
  Commit(event);
}

void UiLayoutState::Apply(SetUiLayoutOverrideEvent const& event) {
  override_root = event.root;
  has_override = true;
  NoteMaterializedChange();
}

void UiLayoutState::Apply(ClearUiLayoutOverrideEvent const&) {
  has_override = false;
  override_root = {};
  NoteMaterializedChange();
}

}  // namespace apptraverse::ui
