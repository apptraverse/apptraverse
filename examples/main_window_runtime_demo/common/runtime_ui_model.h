#ifndef APPTRAVERSE_EXAMPLE_RUNTIME_UI_MODEL_H_
#define APPTRAVERSE_EXAMPLE_RUNTIME_UI_MODEL_H_
#include <functional>
#include <vector>
#include "apptraverse/node_for.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_push_button_presenter.h"
namespace apptraverse::example::runtime_ui {
class RuntimeApplication;
class RuntimeButton : public NodeFor<RuntimeButton, ui::UiPushButton> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::runtime_ui::RuntimeButton",
                           RuntimeButton, ui::UiPushButton, 0)
 protected:
  RuntimeButton() = default;

 public:
  explicit RuntimeButton(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(application), AE_MMBR(number), AE_MMBR(presenter))
  template <class D>
  void Load(ae::Version<0>, D& d) {
    d(base_, application, number, presenter);
  }
  template <class D>
  void Save(ae::Version<0>, D& d) const {
    d(base_, application, number, presenter);
  }
  ae::ObjPtr<RuntimeApplication> application;
  std::uint32_t
      number{};  // Zero is Add; positive numbers identify dynamic Items.
  ui::UiPushButtonPresenter::ptr presenter;
  void HandlePushButtonClicked() override;
  void QueueClickFromUi(ModelObjectProxy& proxy) const override;
  void CollectHistoricalCaptureLiveReferenceIds(
      std::set<ae::ObjId>& ids) const override {
    ids.insert(application.id());
  }
};
// Repeat the immutable Item constraint template over published Item identities.
// The cache is UI/model-runtime derivation, never persistent geometry.
class RuntimeItemLayoutState
    : public NodeFor<RuntimeItemLayoutState, ui::UiLayoutState> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::runtime_ui::RuntimeItemLayoutState",
      RuntimeItemLayoutState, ui::UiLayoutState, 0)
 protected:
  RuntimeItemLayoutState() = default;

 public:
  explicit RuntimeItemLayoutState(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(application))
  template <class D>
  void Load(ae::Version<0>, D& d) {
    d(base_, application);
  }
  template <class D>
  void Save(ae::Version<0>, D& d) const {
    d(base_, application);
  }
  ae::ObjPtr<RuntimeApplication> application;
  ui::UiLayoutNode const& EffectiveRoot() const override;
  void CollectHistoricalCaptureLiveReferenceIds(
      std::set<ae::ObjId>& ids) const override {
    ui::UiLayoutState::CollectHistoricalCaptureLiveReferenceIds(ids);
    ids.insert(application.id());
  }

 private:
  mutable ui::UiLayoutNode projected_root_;
};
class AddRuntimeItemEvent;
class RemoveRuntimeItemEvent;
struct RuntimePresenterFactories {
  std::function<ui::UiWindowPresenter::ptr(ae::Domain&)> window;
  std::function<ui::UiPushButtonPresenter::ptr(ae::Domain&)> button;
};
class RuntimeApplication : public NodeFor<RuntimeApplication> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::runtime_ui::RuntimeApplication",
      RuntimeApplication, Node, 0)
 protected:
  RuntimeApplication() = default;

 public:
  explicit RuntimeApplication(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(window), AE_MMBR(add), AE_MMBR(items))
  template <class D>
  void Load(ae::Version<0>, D& d) {
    d(base_, window, add, items);
  }
  template <class D>
  void Save(ae::Version<0>, D& d) const {
    d(base_, window, add, items);
  }
  ui::UiDesktopWindow::ptr window;
  RuntimeButton::ptr add;
  std::vector<RuntimeButton::ptr> items;
  RuntimePresenterFactories
      factories;  // Presentation constructors, runtime only.
  void AddItem();
  void RemoveItem(ae::ObjId id);
  void Apply(AddRuntimeItemEvent const& event);
  void Apply(RemoveRuntimeItemEvent const& event);
};
class AddRuntimeItemEvent
    : public EventFor<RuntimeApplication, AddRuntimeItemEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::runtime_ui::AddRuntimeItemEvent",
      AddRuntimeItemEvent, Event, 0)
 protected:
  AddRuntimeItemEvent() = default;

 public:
  explicit AddRuntimeItemEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(item))
  RuntimeButton::ptr item;
};
class RemoveRuntimeItemEvent
    : public EventFor<RuntimeApplication, RemoveRuntimeItemEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::runtime_ui::RemoveRuntimeItemEvent",
      RemoveRuntimeItemEvent, Event, 0)
 protected:
  RemoveRuntimeItemEvent() = default;

 public:
  explicit RemoveRuntimeItemEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(item_id))
  ae::ObjId item_id;
};
ae::ObjId RuntimeApplicationId();
ui::UiLocalizationString::ptr MakeRuntimeUiLabel(ae::Domain& domain,
                                                 Node& parent,
                                                 std::string text);
RuntimeButton::ptr MakeRuntimeUiButton(RuntimeApplication& app,
                                       std::uint32_t number);
void PrepareRuntimeUi(RuntimeApplication& app,
                      RuntimePresenterFactories factories);
void EnsureRuntimeUiRegistration();
}  // namespace apptraverse::example::runtime_ui
#endif
