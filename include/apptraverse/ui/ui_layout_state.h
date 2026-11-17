#ifndef APPTRAVERSE_UI_UI_LAYOUT_STATE_H_
#define APPTRAVERSE_UI_UI_LAYOUT_STATE_H_

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/ui/ui_constraint_layout.h"

namespace apptraverse::ui {

// Supply-owned definition. Its identity remains stable across compatible
// supply revisions; a user's layout state never copies the default tree.
class UiLayoutDefinition : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiLayoutDefinition",
                          UiLayoutDefinition, ae::Obj, 0)
 protected:
  UiLayoutDefinition() = default;
 public:
  explicit UiLayoutDefinition(ae::ObjProp prop) : Obj{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(root))
  template <typename Dnv> void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, root);
  }
  template <typename Dnv> void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, root);
  }
  UiLayoutNode root;
};

class SetUiLayoutOverrideEvent;
class ClearUiLayoutOverrideEvent;

// Portable model state. Metrics, pixels, measured rectangles and the solver
// cache belong exclusively to each local presentation, never this Node.
class UiLayoutState : public NodeFor<UiLayoutState> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiLayoutState",
                          UiLayoutState, Node, 0)
 protected:
  UiLayoutState() = default;
 public:
  explicit UiLayoutState(ae::ObjProp prop) : NodeFor{prop}, has_override{false} {}
  AE_OBJECT_REFLECT(AE_MMBR(definition), AE_MMBR(has_override),
                    AE_MMBR(override_root))
  template <typename Dnv> void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, definition, has_override, override_root);
  }
  template <typename Dnv> void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, definition, has_override, override_root);
  }
  UiLayoutDefinition::ptr definition;
  bool has_override;
  UiLayoutNode override_root;

  void Bootstrap(UiLayoutDefinition::ptr value, bool overridden, UiLayoutNode root) {
    definition = std::move(value);
    has_override = overridden;
    override_root = std::move(root);
  }
  UiLayoutNode const& EffectiveRoot() const;
  bool TrySetOverride(UiLayoutNode root);
  void SetOverride(UiLayoutNode root);
  void ClearOverride();
  void Apply(SetUiLayoutOverrideEvent const& event);
  void Apply(ClearUiLayoutOverrideEvent const& event);
  void CollectHistoricalCaptureLiveReferenceIds(
      std::set<ae::ObjId>& ids) const override {
    // A supply reference must remain a reference during journal compaction and
    // replay, otherwise the historical base freezes an obsolete default tree.
    ids.insert(definition.id());
  }
};

class SetUiLayoutOverrideEvent
    : public EventFor<UiLayoutState, SetUiLayoutOverrideEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::SetUiLayoutOverrideEvent",
                          SetUiLayoutOverrideEvent, Event, 0)
 protected:
  SetUiLayoutOverrideEvent() = default;
 public:
  explicit SetUiLayoutOverrideEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(root))
  UiLayoutNode root;
};

class ClearUiLayoutOverrideEvent
    : public EventFor<UiLayoutState, ClearUiLayoutOverrideEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::ClearUiLayoutOverrideEvent",
                          ClearUiLayoutOverrideEvent, Event, 0)
 protected:
  ClearUiLayoutOverrideEvent() = default;
 public:
  explicit ClearUiLayoutOverrideEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT()
};

}  // namespace apptraverse::ui
#endif
