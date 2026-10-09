#ifndef APPTRAVERSE_UI_UI_PRESENTERS_H_
#define APPTRAVERSE_UI_UI_PRESENTERS_H_

#include "apptraverse/presenter.h"

#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_edit_box.h"
#include "apptraverse/ui/ui_label.h"
#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_push_button.h"

namespace apptraverse::ui {

class UiDesktopWindowPresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiDesktopWindowPresenter",
                           UiDesktopWindowPresenter, Presenter, 0)

 protected:
  UiDesktopWindowPresenter() = default;

 public:
  explicit UiDesktopWindowPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window), AE_MMBR(title), AE_MMBR(locale))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, window, title, locale);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, window, title, locale);
  }

  UiDesktopWindow::ptr window;
  ae::ObjPtr<class UiLocalizationString> title;
  ae::ObjPtr<UiLocaleSettings> locale;

  void SubmitFrameFromNative(std::int32_t x, std::int32_t y, std::int32_t width,
                             std::int32_t height);
};

class UiEditBoxPresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiEditBoxPresenter",
                           UiEditBoxPresenter, Presenter, 0)

 protected:
  UiEditBoxPresenter() = default;

 public:
  explicit UiEditBoxPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(edit_box), AE_MMBR(window_presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, edit_box, window_presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, edit_box, window_presenter);
  }

  UiEditBox::ptr edit_box;
  ae::ObjPtr<UiDesktopWindowPresenter> window_presenter;

  bool ReadyForPresentation() const override;

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

  AE_OBJECT_REFLECT(AE_MMBR(button), AE_MMBR(window_presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, button, window_presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, button, window_presenter);
  }

  UiPushButton::ptr button;
  ae::ObjPtr<UiDesktopWindowPresenter> window_presenter;

  bool ReadyForPresentation() const override;

  bool OnCommand(std::uint32_t command_id,
                 std::uint16_t notification_code) override;

  void SimulateClickForTest();
};

class UiLabelPresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiLabelPresenter", UiLabelPresenter,
                           Presenter, 0)

 protected:
  UiLabelPresenter() = default;

 public:
  explicit UiLabelPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label), AE_MMBR(window_presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, label, window_presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, label, window_presenter);
  }

  UiLabel::ptr label;
  ae::ObjPtr<UiDesktopWindowPresenter> window_presenter;

  bool ReadyForPresentation() const override;
};

void EnsureUiPresenterRegistration();

}  // namespace apptraverse::ui

#endif  // APPTRAVERSE_UI_UI_PRESENTERS_H_
