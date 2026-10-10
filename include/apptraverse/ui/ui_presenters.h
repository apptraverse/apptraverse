#ifndef APPTRAVERSE_UI_UI_PRESENTERS_H_
#define APPTRAVERSE_UI_UI_PRESENTERS_H_

#include "apptraverse/presenter.h"

#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_edit_box.h"
#include "apptraverse/ui/ui_label.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_push_button.h"
#include "apptraverse/ui/ui_window.h"

namespace apptraverse::ui {

struct UiControlLayout {
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{100};
  std::int32_t height{24};
  std::uint32_t control_id{0};
};

class UiWindowPresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiWindowPresenter",
                           UiWindowPresenter, Presenter, 0)

 protected:
  UiWindowPresenter() = default;

 public:
  explicit UiWindowPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window), AE_MMBR(title), AE_MMBR(locale))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, window, title, locale);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, window, title, locale);
  }

  UiWindow::ptr window;
  ae::ObjPtr<class UiLocalizationString> title;
  ae::ObjPtr<UiLocaleSettings> locale;
};

class UiDesktopWindowPresenter : public UiWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiDesktopWindowPresenter",
                           UiDesktopWindowPresenter, UiWindowPresenter, 0)

 protected:
  UiDesktopWindowPresenter() = default;

 public:
  explicit UiDesktopWindowPresenter(ae::ObjProp prop)
      : UiWindowPresenter{prop} {}

  UiDesktopWindow::ptr desktop_window() const {
    return window ? UiDesktopWindow::ptr::MakeFromThis(
                        static_cast<UiDesktopWindow*>(&*window))
                  : UiDesktopWindow::ptr{};
  }

  void SubmitFrameFromNative(std::int32_t x, std::int32_t y,
                             std::int32_t client_width,
                             std::int32_t client_height);
};

class UiEditBoxPresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiEditBoxPresenter",
                           UiEditBoxPresenter, Presenter, 0)

 protected:
  UiEditBoxPresenter() = default;

 public:
  explicit UiEditBoxPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(edit_box), AE_MMBR(window_presenter),
                    AE_MMBR(layout_x), AE_MMBR(layout_y), AE_MMBR(layout_width),
                    AE_MMBR(layout_height), AE_MMBR(control_id))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, edit_box, window_presenter, layout_x, layout_y, layout_width,
        layout_height, control_id);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, edit_box, window_presenter, layout_x, layout_y, layout_width,
        layout_height, control_id);
  }

  UiEditBox::ptr edit_box;
  ae::ObjPtr<UiWindowPresenter> window_presenter;
  std::int32_t layout_x{0};
  std::int32_t layout_y{0};
  std::int32_t layout_width{200};
  std::int32_t layout_height{24};
  std::uint32_t control_id{0};

  bool ReadyForPresentation() const override;

  bool OnCommand(std::uint32_t command_id,
                 std::uint16_t notification_code) override;

  void SubmitTextFromUi(std::string text, std::size_t caret_utf8_offset);
  void SubmitCaretFromUi(std::size_t caret_utf8_offset);
};

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
  std::int32_t layout_x{0};
  std::int32_t layout_y{0};
  std::int32_t layout_width{120};
  std::int32_t layout_height{28};
  std::uint32_t control_id{0};

  bool ReadyForPresentation() const override;

  bool OnCommand(std::uint32_t command_id,
                 std::uint16_t notification_code) override;

  void SimulateClickForTest();
};

class UiListContainerPresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiListContainerPresenter",
                           UiListContainerPresenter, Presenter, 0)

 protected:
  UiListContainerPresenter() = default;

 public:
  explicit UiListContainerPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(container), AE_MMBR(window_presenter),
                    AE_MMBR(layout_x), AE_MMBR(layout_y),
                    AE_MMBR(layout_width), AE_MMBR(layout_height),
                    AE_MMBR(control_id))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, container, window_presenter, layout_x, layout_y, layout_width,
        layout_height, control_id);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, container, window_presenter, layout_x, layout_y, layout_width,
        layout_height, control_id);
  }

  UiListContainer::ptr container;
  ae::ObjPtr<UiWindowPresenter> window_presenter;
  std::int32_t layout_x{0};
  std::int32_t layout_y{0};
  std::int32_t layout_width{200};
  std::int32_t layout_height{120};
  std::uint32_t control_id{0};

  bool ReadyForPresentation() const override;
  void OnModelChanged() override;
};

class UiLabelPresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiLabelPresenter", UiLabelPresenter,
                           Presenter, 0)

 protected:
  UiLabelPresenter() = default;

 public:
  explicit UiLabelPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label), AE_MMBR(window_presenter), AE_MMBR(layout_x),
                    AE_MMBR(layout_y), AE_MMBR(layout_width),
                    AE_MMBR(layout_height))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, label, window_presenter, layout_x, layout_y, layout_width,
        layout_height);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, label, window_presenter, layout_x, layout_y, layout_width,
        layout_height);
  }

  UiLabel::ptr label;
  ae::ObjPtr<UiWindowPresenter> window_presenter;
  std::int32_t layout_x{0};
  std::int32_t layout_y{0};
  std::int32_t layout_width{200};
  std::int32_t layout_height{20};
};

void EnsureUiPresenterRegistration();

}  // namespace apptraverse::ui

#endif  // APPTRAVERSE_UI_UI_PRESENTERS_H_
