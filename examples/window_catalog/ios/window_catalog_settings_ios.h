#pragma once

#include "window_catalog_menu.h"

namespace apptraverse {

class SetIosCatalogSettingsPresentedEvent;

class IosCatalogUiState : public NodeFor<IosCatalogUiState, CatalogUiState> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::IosCatalogUiState", IosCatalogUiState,
                          CatalogUiState, 0)
 protected:
  IosCatalogUiState() = default;
 public:
  explicit IosCatalogUiState(ae::ObjProp prop) : NodeFor{prop} {}

  bool settings_presented;
  AE_OBJECT_REFLECT(AE_MMBR(settings_presented))
  template<class D> void Load(ae::Version<0>, D& d) { d(base_, settings_presented); }
  template<class D> void Save(ae::Version<0>, D& d) const { d(base_, settings_presented); }

  using CatalogUiState::Apply;
  void Apply(SetIosCatalogSettingsPresentedEvent const& event);
  void SetSettingsPresentedFromUi(bool presented);
};

class SetIosCatalogSettingsPresentedEvent
    : public EventFor<IosCatalogUiState, SetIosCatalogSettingsPresentedEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::SetIosCatalogSettingsPresentedEvent",
                          SetIosCatalogSettingsPresentedEvent, Event, 0)
 protected:
  SetIosCatalogSettingsPresentedEvent() = default;
 public:
  explicit SetIosCatalogSettingsPresentedEvent(ae::ObjProp prop) : EventFor{prop} {}
  bool presented;
  AE_OBJECT_REFLECT(AE_MMBR(presented))
};

}  // namespace apptraverse
