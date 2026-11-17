#ifndef APPTRAVERSE_UI_UI_WINDOW_PRESENTER_H_
#define APPTRAVERSE_UI_UI_WINDOW_PRESENTER_H_

#include "apptraverse/presenter.h"

#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_window.h"
#include "apptraverse/ui/ui_constraint_layout.h"

namespace apptraverse::ui {

struct UiControlLayout {
  std::int32_t x;
  std::int32_t y;
  std::int32_t width;
  std::int32_t height;
  std::uint32_t control_id;
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

  // Presentation policy only: a native window can provide an action itself.
  // The prefab links remain intact even when its content control is omitted.
  virtual bool NativeCaptionProvidesClose() const { return false; }
  virtual bool PresentsChildControl(Presenter const& child) const;
  virtual UiLayoutNode const* LayoutRoot() const;
  virtual class UiDeviceState* LocalDeviceState() const;

  virtual void* NativeChildParentHandle() const { return nullptr; }

  virtual UiControlLayout ResolveChildLayout(UiControlLayout requested) const {
    return requested;
  }

  virtual std::string ResolvePresentedWindowTitle(int language_index) const;
 private:
  mutable UiLayoutNode presented_layout_;
};

}  // namespace apptraverse::ui

#endif  // APPTRAVERSE_UI_UI_WINDOW_PRESENTER_H_
