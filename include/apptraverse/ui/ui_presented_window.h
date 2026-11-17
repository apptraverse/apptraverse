#ifndef APPTRAVERSE_UI_UI_PRESENTED_WINDOW_H_
#define APPTRAVERSE_UI_UI_PRESENTED_WINDOW_H_

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

#include "apptraverse/ui/ui_window.h"
#include "apptraverse/ui/ui_layout_state.h"
#include "apptraverse/ui/ui_device_state.h"
#include "apptraverse/ui/ui_window_presenter.h"

namespace apptraverse::ui {

class UiPresentedWindow : public NodeFor<UiPresentedWindow, UiWindow> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiPresentedWindow",
                           UiPresentedWindow, UiWindow, 0)

 protected:
  UiPresentedWindow() = default;

 public:
  explicit UiPresentedWindow(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(presenter), AE_MMBR(layout_state), AE_MMBR(device_state),
                    AE_MMBR(caption_close_control), AE_MMBR(caption_close_control_id))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, presenter, layout_state, device_state, caption_close_control, caption_close_control_id);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, presenter, layout_state, device_state, caption_close_control, caption_close_control_id);
  }

  ae::ObjPtr<UiWindowPresenter> presenter;
  UiLayoutState::ptr layout_state;
  UiDeviceState::ptr device_state;
  ae::ObjPtr<Presenter> caption_close_control;
  std::uint32_t caption_close_control_id{};

  void CollectHistoricalCaptureLiveReferenceIds(std::set<ae::ObjId>& ids) const override {
    ids.insert(device_state.id());
    // Nested UiLayoutState supplies its definition policy through generic capture.
  }
};

}  // namespace apptraverse::ui

#endif  // APPTRAVERSE_UI_UI_PRESENTED_WINDOW_H_
