#include "apptraverse/ui/ui_text_control.h"
#include <algorithm>
#include "apptraverse/model_object_proxy.h"
namespace apptraverse::ui {
APPTRAVERSE_REGISTER(UiTextControl);
APPTRAVERSE_REGISTER(UiTextEditedEvent);
APPTRAVERSE_REGISTER(UiTextAcceptedEvent);
APPTRAVERSE_REGISTER(UiTextControlPresenter);
void EnsureUiTextControlRegistration() {
  (void)&g_apptraverse_registrar_UiTextControl;
}
void UiTextControl::Materialize(std::string value,
                                std::vector<std::string> values, bool available,
                                bool shown, bool readonly) {
  if (text == value && rows == values && enabled == available &&
      visible == shown && read_only == readonly)
    return;
  text = std::move(value);
  rows = std::move(values);
  enabled = available;
  visible = shown;
  read_only = readonly;
  NoteMaterializedChange();
}
void UiTextControl::MaterializeAccessibleLabel(std::string value) {
  if (accessible_label == value) return;
  accessible_label = std::move(value);
  NoteMaterializedChange();
}
void UiTextControl::SubmitText(std::string value, std::uint64_t sequence) {
  if (!enabled || read_only) return;
  auto e = UiTextEditedEvent::ptr::Create(ae::CreateWith{*domain});
  e->text = std::move(value);
  e->revision = sequence;
  Commit(e);
  HandleTextEdited();
}
void UiTextControl::SubmitAccept() {
  if (!enabled || read_only) return;
  auto e = UiTextAcceptedEvent::ptr::Create(ae::CreateWith{*domain});
  Commit(e);
  HandleTextAccepted();
}
void UiTextControl::Apply(UiTextEditedEvent const& e) {
  text = e.text;
  revision = e.revision;
  NoteMaterializedChange();
}
void UiTextControl::Apply(UiTextAcceptedEvent const&) {}
void UiTextControlPresenter::QueueEditedFromUi(std::string value) {
  pending_edit = std::max(pending_edit, control->revision) + 1;
  model_proxy->Invoke(control.id(), &UiTextControl::SubmitText,
                      std::move(value), pending_edit);
}
void UiTextControlPresenter::QueueAcceptFromUi() {
  model_proxy->Invoke(control.id(), &UiTextControl::SubmitAccept);
}
}  // namespace apptraverse::ui
