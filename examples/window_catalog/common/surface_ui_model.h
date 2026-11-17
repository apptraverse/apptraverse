#ifndef APPTRAVERSE_EXAMPLES_SURFACE_UI_MODEL_H_
#define APPTRAVERSE_EXAMPLES_SURFACE_UI_MODEL_H_
#include <functional>
#include <vector>
#include "apptraverse/node_for.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_presenters.h"

// The historical Surface library remains an independent versioning fixture.
// This S0 application composes the ordinary UI components and generic hosts.
namespace apptraverse::example::surface_ui {
class SurfaceApplication;
class SurfaceWindow;
class SurfaceAction : public NodeFor<SurfaceAction, ui::UiPushButton> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surface_ui::Action", SurfaceAction, ui::UiPushButton, 0)
 protected:
  SurfaceAction() = default;
 public:
  explicit SurfaceAction(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(application), AE_MMBR(window_id), AE_MMBR(role), AE_MMBR(presenter))
  template <class D> void Load(ae::Version<0>, D& d) { d(base_, application, window_id, role, presenter); }
  template <class D> void Save(ae::Version<0>, D& d) const { d(base_, application, window_id, role, presenter); }
  ae::ObjPtr<SurfaceApplication> application;
  ae::ObjId window_id;
  std::uint32_t role; // 1001 Add, 1002 Remove this window, 1003 Language.
  ui::UiPushButtonPresenter::ptr presenter;
  void HandlePushButtonClicked() override;
  void QueueClickFromUi(ModelObjectProxy& proxy) const override;
  void CollectHistoricalCaptureLiveReferenceIds(std::set<ae::ObjId>& ids) const override { ids.insert(application.id()); }
};
class SurfaceWindow : public NodeFor<SurfaceWindow, ui::UiDesktopWindow> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surface_ui::Window", SurfaceWindow, ui::UiDesktopWindow, 0)
 protected:
  SurfaceWindow() = default;
 public:
  explicit SurfaceWindow(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(number), AE_MMBR(actions))
  template <class D> void Load(ae::Version<0>, D& d) { d(base_, number, actions); }
  template <class D> void Save(ae::Version<0>, D& d) const { d(base_, number, actions); }
  std::uint32_t number;
  std::vector<SurfaceAction::ptr> actions;
};
struct SurfaceFactories {
  std::function<ui::UiDesktopWindowPresenter::ptr(ae::Domain&)> window;
  std::function<ui::UiPushButtonPresenter::ptr(ae::Domain&)> button;
};
class AddSurfaceUiEvent;
class RemoveSurfaceUiEvent;
class SelectSurfaceUiEvent;
class OrderSurfaceUiEvent;
struct SurfaceResources {
  ui::UiLocaleSettings::ptr locale;
  ui::UiDeviceState::ptr device;
  ui::UiLayoutDefinition::ptr definition;
  ui::UiLocalizationString::ptr add_label, close_label, language_label;
  AE_REFLECT_MEMBERS(locale, device, definition, add_label, close_label, language_label)
};
class SurfaceApplication : public NodeFor<SurfaceApplication> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surface_ui::Application", SurfaceApplication, Node, 0)
 protected:
  SurfaceApplication() = default;
 public:
  explicit SurfaceApplication(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(windows), AE_MMBR(current), AE_MMBR(order), AE_MMBR(next_number), AE_MMBR(v1), AE_MMBR(resources))
  template <class D> void Load(ae::Version<0>, D& d) { d(base_, windows, current, order, next_number, v1, resources); }
  template <class D> void Save(ae::Version<0>, D& d) const { d(base_, windows, current, order, next_number, v1, resources); }
  std::vector<SurfaceWindow::ptr> windows;
  ae::ObjId current;
  std::vector<ae::ObjId> order;
  std::uint32_t next_number;
  bool v1;
  SurfaceResources resources;
  void CollectHistoricalCaptureLiveReferenceIds(std::set<ae::ObjId>& ids) const override {
    ids.insert(resources.device.id());
    ids.insert(resources.definition.id());
  }
  SurfaceFactories factories; // Runtime capabilities, never serialized.
  std::function<void()> stop_when_empty;
  ae::ObjId focus_request; // One-shot model command intent, never replayed.
  void Add();
  void PerformAction(ae::ObjId window_id, std::uint32_t role);
  void Remove(ae::ObjId id);
  void Select(ae::ObjId id);
  void SetOrder(std::vector<std::uint32_t> ids);
  void SwitchLanguage();
  void Apply(AddSurfaceUiEvent const&);
  void Apply(RemoveSurfaceUiEvent const&);
  void Apply(SelectSurfaceUiEvent const&);
  void Apply(OrderSurfaceUiEvent const&);
};
class AddSurfaceUiEvent : public EventFor<SurfaceApplication, AddSurfaceUiEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surface_ui::Add", AddSurfaceUiEvent, Event, 0)
 protected: AddSurfaceUiEvent() = default;
 public:
  explicit AddSurfaceUiEvent(ae::ObjProp p) : EventFor{p} {}
  AE_OBJECT_REFLECT(AE_MMBR(window))
  SurfaceWindow::ptr window;
};
class RemoveSurfaceUiEvent : public EventFor<SurfaceApplication, RemoveSurfaceUiEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surface_ui::Remove", RemoveSurfaceUiEvent, Event, 0)
 protected: RemoveSurfaceUiEvent() = default;
 public:
  explicit RemoveSurfaceUiEvent(ae::ObjProp p) : EventFor{p} {}
  AE_OBJECT_REFLECT(AE_MMBR(window_id))
  ae::ObjId window_id;
};
class SelectSurfaceUiEvent : public EventFor<SurfaceApplication, SelectSurfaceUiEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surface_ui::Select", SelectSurfaceUiEvent, Event, 0)
 protected: SelectSurfaceUiEvent() = default;
 public:
  explicit SelectSurfaceUiEvent(ae::ObjProp p) : EventFor{p} {}
  AE_OBJECT_REFLECT(AE_MMBR(window_id))
  ae::ObjId window_id;
};
class OrderSurfaceUiEvent : public EventFor<SurfaceApplication, OrderSurfaceUiEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surface_ui::Order", OrderSurfaceUiEvent, Event, 0)
 protected: OrderSurfaceUiEvent() = default;
 public:
  explicit OrderSurfaceUiEvent(ae::ObjProp p) : EventFor{p} {}
  AE_OBJECT_REFLECT(AE_MMBR(ids))
  std::vector<ae::ObjId> ids;
};
ui::UiLocalizationString::ptr MakeSurfaceLabel(ae::Domain&, Node&, std::string, std::string, int);
SurfaceWindow::ptr MakeSurfaceWindow(SurfaceApplication&, std::uint32_t);
ae::ObjId ApplicationId();
SurfaceApplication::ptr BuildSurfaceUi(ae::Domain&, SurfaceFactories, bool v1);
void PrepareSurfaceUi(SurfaceApplication&, SurfaceFactories);
int SaveSurfaceUi(SurfaceApplication&);
void EnsureSurfaceUiRegistration();
} // namespace apptraverse::example::surface_ui
#endif
