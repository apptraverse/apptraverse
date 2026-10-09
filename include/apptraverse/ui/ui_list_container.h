#ifndef APPTRAVERSE_UI_UI_LIST_CONTAINER_H_
#define APPTRAVERSE_UI_UI_LIST_CONTAINER_H_

#include <string>
#include <vector>

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/presenter.h"

#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_push_button.h"

namespace apptraverse::ui {

class UiListRowPresenter;

class UiListRow : public NodeFor<UiListRow> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiListRow", UiListRow, Node, 0)

 protected:
  UiListRow() = default;

 public:
  explicit UiListRow(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(primary_text), AE_MMBR(delete_button),
                    AE_MMBR(presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, primary_text, delete_button, presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, primary_text, delete_button, presenter);
  }

  ae::ObjPtr<UiLocalizationString> primary_text;
  ae::ObjPtr<UiPushButton> delete_button;
  ae::ObjPtr<UiListRowPresenter> presenter;
};

class RemoveUiListRowEvent;

class UiListContainer : public NodeFor<UiListContainer> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiListContainer", UiListContainer,
                           Node, 0)

 protected:
  UiListContainer() = default;

 public:
  explicit UiListContainer(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(rows))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, rows);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, rows);
  }

  std::vector<UiListRow::ptr> rows;

  void Apply(RemoveUiListRowEvent const& event);
};

class RemoveUiListRowEvent
    : public EventFor<UiListContainer, RemoveUiListRowEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::RemoveUiListRowEvent",
                           RemoveUiListRowEvent, Event, 0)

 protected:
  RemoveUiListRowEvent() = default;

 public:
  explicit RemoveUiListRowEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(row_id))

  ae::ObjId row_id;
};

class UiListRowPresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiListRowPresenter",
                           UiListRowPresenter, Presenter, 0)

 protected:
  UiListRowPresenter() = default;

 public:
  explicit UiListRowPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(row))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, row);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, row);
  }

  UiListRow::ptr row;
};

}  // namespace apptraverse::ui

#endif  // APPTRAVERSE_UI_UI_LIST_CONTAINER_H_
