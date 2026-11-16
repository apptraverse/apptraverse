#ifndef APPTRAVERSE_EXAMPLE_SURFACES_DEMO_LANGUAGE_BUTTON_H_

#define APPTRAVERSE_EXAMPLE_SURFACES_DEMO_LANGUAGE_BUTTON_H_



#include "apptraverse/object_macros.h"

#include "apptraverse/ui/ui_locale_settings.h"

#include "apptraverse/ui/ui_push_button.h"



namespace apptraverse::example::surfaces {



class SurfacesDemoLanguageButton : public ui::UiPushButton {

  APPTRAVERSE_NAMED_OBJECT(

      "apptraverse::example::surfaces::SurfacesDemoLanguageButton",

      SurfacesDemoLanguageButton, ui::UiPushButton, 0)



 protected:

  SurfacesDemoLanguageButton() = default;



 public:

  explicit SurfacesDemoLanguageButton(ae::ObjProp prop) : ui::UiPushButton{prop} {}



  AE_OBJECT_REFLECT(AE_MMBR(locale))



  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, locale);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, locale);
  }



  ae::ObjPtr<ui::UiLocaleSettings> locale;



  void HandlePushButtonClicked() override;

};



}  // namespace apptraverse::example::surfaces



#endif  // APPTRAVERSE_EXAMPLE_SURFACES_DEMO_LANGUAGE_BUTTON_H_

