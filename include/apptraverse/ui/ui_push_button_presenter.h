#ifndef APPTRAVERSE_UI_UI_PUSH_BUTTON_PRESENTER_H_
#define APPTRAVERSE_UI_UI_PUSH_BUTTON_PRESENTER_H_

#include "apptraverse/presenter.h"

#include "apptraverse/ui/ui_push_button.h"

namespace apptraverse::ui {

class UiWindowPresenter;

class UiPushButtonPresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiPushButtonPresenter",
                           UiPushButtonPresenter, Presenter, 0)

 protected:
  UiPushButtonPresenter() = default;

 public:
  explicit UiPushButtonPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(button), AE_MMBR(window_presenter), AE_MMBR(layout_x),
                    AE_MMBR(layout_y), AE_MMBR(layout_width),
                    AE_MMBR(layout_height), AE_MMBR(control_id))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, button, window_presenter, layout_x, layout_y, layout_width,
        layout_height, control_id);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, button, window_presenter, layout_x, layout_y, layout_width,
        layout_height, control_id);
  }

  UiPushButton::ptr button;
  ae::ObjPtr<UiWindowPresenter> window_presenter;
  std::int32_t layout_x;
  std::int32_t layout_y;
  std::int32_t layout_width;
  std::int32_t layout_height;
  std::uint32_t control_id;

  bool ReadyForPresentation() const override;

  void NotifyClickedFromUi() override;

  void SimulateClickForTest();
};

}  // namespace apptraverse::ui

#endif  // APPTRAVERSE_UI_UI_PUSH_BUTTON_PRESENTER_H_
