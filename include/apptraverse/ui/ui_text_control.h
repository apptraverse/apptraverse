#pragma once
#include <string>
#include <vector>
#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/ui/ui_window_presenter.h"
namespace apptraverse::ui {
enum class UiTextControlKind : unsigned { kLabel, kEdit, kMultiline };
class UiTextEditedEvent;
class UiTextAcceptedEvent;
// Text-bearing native controls share one model contract. No measured geometry
// or native editing resources are serialized here.
class UiTextControl : public NodeFor<UiTextControl> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiTextControl", UiTextControl,
                           Node, 0)
 protected:
  UiTextControl() = default;

 public:
  explicit UiTextControl(ae::ObjProp p) : NodeFor{p} {}
  std::string text;
  std::string accessible_label;
  std::vector<std::string> rows;
  UiTextControlKind kind{UiTextControlKind::kLabel};
  bool enabled{true}, visible{true}, read_only{false};
  std::uint64_t revision{};
  AE_OBJECT_REFLECT(AE_MMBR(text), AE_MMBR(accessible_label), AE_MMBR(rows),
                    AE_MMBR(kind), AE_MMBR(enabled), AE_MMBR(visible),
                    AE_MMBR(read_only), AE_MMBR(revision))
  template <class D>
  void Load(ae::Version<0>, D& d) {
    d(base_, text, accessible_label, rows, kind, enabled, visible, read_only,
      revision);
  }
  template <class D>
  void Save(ae::Version<0>, D& d) const {
    d(base_, text, accessible_label, rows, kind, enabled, visible, read_only,
      revision);
  }
  // A parent model Event may materialize a derived text projection. The edit
  // sequence remains that of the last accepted native edit.
  void Materialize(std::string value, std::vector<std::string> values,
                   bool available, bool shown, bool readonly);
  void MaterializeAccessibleLabel(std::string value);
  void SubmitText(std::string value, std::uint64_t sequence);
  void SubmitAccept();
  void Apply(UiTextEditedEvent const&);
  void Apply(UiTextAcceptedEvent const&);
  virtual void HandleTextEdited() {}
  virtual void HandleTextAccepted() {}
};
class UiTextEditedEvent : public EventFor<UiTextControl, UiTextEditedEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiTextEditedEvent",
                           UiTextEditedEvent, Event, 0)
 protected:
  UiTextEditedEvent() = default;

 public:
  explicit UiTextEditedEvent(ae::ObjProp p) : EventFor{p} {}
  std::string text;
  std::uint64_t revision{};
  AE_OBJECT_REFLECT(AE_MMBR(text), AE_MMBR(revision))
};
class UiTextAcceptedEvent
    : public EventFor<UiTextControl, UiTextAcceptedEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiTextAcceptedEvent",
                           UiTextAcceptedEvent, Event, 0)
 protected:
  UiTextAcceptedEvent() = default;

 public:
  explicit UiTextAcceptedEvent(ae::ObjProp p) : EventFor{p} {}
  AE_OBJECT_REFLECT()
};
class UiTextControlPresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiTextControlPresenter",
                           UiTextControlPresenter, Presenter, 0)
 protected:
  UiTextControlPresenter() = default;

 public:
  explicit UiTextControlPresenter(ae::ObjProp p) : Presenter{p} {}
  UiTextControl::ptr control;
  UiWindowPresenter::ptr window_presenter;
  std::uint32_t control_id{};
  AE_OBJECT_REFLECT(AE_MMBR(control), AE_MMBR(window_presenter),
                    AE_MMBR(control_id))
  template <class D>
  void Load(ae::Version<0>, D& d) {
    d(base_, control, window_presenter, control_id);
  }
  template <class D>
  void Save(ae::Version<0>, D& d) const {
    d(base_, control, window_presenter, control_id);
  }
  bool ReadyForPresentation() const override {
    return window_presenter->presentation_loaded;
  }
  void QueueEditedFromUi(std::string value);
  void QueueAcceptFromUi();
  // Runtime edit acknowledgement; stale publications must not reset native
  // input.
  std::uint64_t pending_edit{};
};
void EnsureUiTextControlRegistration();
}  // namespace apptraverse::ui
