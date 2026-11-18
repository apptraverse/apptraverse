#include "AppleComposition.h"
namespace apptraverse::chat::gui {
namespace {
void Present(Presenter& presenter, ChatUi const& owner, Control role) {
  static_cast<Presentation*>(presenter.presentation_host)->Present(role,owner.Get(role),owner);
}
}
class AppleTextPresenter final : public ui::UiTextControlPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::apple::TextPresenter",AppleTextPresenter,ui::UiTextControlPresenter,0)
 protected: AppleTextPresenter()=default;
 public:
  explicit AppleTextPresenter(ae::ObjProp p):UiTextControlPresenter{p}{}
  AE_OBJECT_REFLECT()
  template<class D> void Load(ae::Version<0>,D& d){d(base_);}
  template<class D> void Save(ae::Version<0>,D& d)const{d(base_);}
  void OnLoad()override{OnModelChanged();}
  void OnModelChanged()override{
    auto const& text=static_cast<ChatTextControl const&>(*control);
    Present(*this,*text.owner,static_cast<Control>(text.role));
  }
  void OnUnload()override{
    auto const& text=static_cast<ChatTextControl const&>(*control);
    static_cast<Presentation*>(presentation_host)->Remove(static_cast<Control>(text.role));
  }
};
class AppleButtonPresenter final : public ui::UiPushButtonPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::apple::ButtonPresenter",AppleButtonPresenter,ui::UiPushButtonPresenter,0)
 protected: AppleButtonPresenter()=default;
 public:
  explicit AppleButtonPresenter(ae::ObjProp p):UiPushButtonPresenter{p}{}
  AE_OBJECT_REFLECT()
  template<class D> void Load(ae::Version<0>,D& d){d(base_);}
  template<class D> void Save(ae::Version<0>,D& d)const{d(base_);}
  void OnLoad()override{OnModelChanged();}
  void OnModelChanged()override{
    auto const& value=static_cast<ChatActionButton const&>(*button);
    Present(*this,*value.owner,static_cast<Control>(value.action));
  }
  void OnUnload()override{
    auto const& value=static_cast<ChatActionButton const&>(*button);
    static_cast<Presentation*>(presentation_host)->Remove(static_cast<Control>(value.action));
  }
};
class AppleListPresenter final : public ui::UiListContainerPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::apple::ListPresenter",AppleListPresenter,ui::UiListContainerPresenter,0)
 protected: AppleListPresenter()=default;
 public:
  explicit AppleListPresenter(ae::ObjProp p):UiListContainerPresenter{p}{}
  AE_OBJECT_REFLECT()
  template<class D> void Load(ae::Version<0>,D& d){d(base_);}
  template<class D> void Save(ae::Version<0>,D& d)const{d(base_);}
  void OnLoad()override{OnModelChanged();}
  void OnModelChanged()override{
    Present(*this,static_cast<ChatUi const&>(*window_presenter->window),Control::People);
  }
  void OnUnload()override{static_cast<Presentation*>(presentation_host)->Remove(Control::People);}
};
APPTRAVERSE_REGISTER(AppleTextPresenter);
APPTRAVERSE_REGISTER(AppleButtonPresenter);
APPTRAVERSE_REGISTER(AppleListPresenter);
void ConfigureAppleComposition(ui::UiDeviceEnvironment baseline){
  EnsureUiRegistration();
  UiFactories factories;
  factories.create_window=[](ae::Domain& d){return ChatWindowPresenter::ptr::Create(ae::CreateWith{d});};
  factories.create_text=[](ae::Domain& d){return AppleTextPresenter::ptr::Create(ae::CreateWith{d});};
  factories.create_button=[](ae::Domain& d){return AppleButtonPresenter::ptr::Create(ae::CreateWith{d});};
  factories.create_list=[](ae::Domain& d){return AppleListPresenter::ptr::Create(ae::CreateWith{d});};
  factories.prepare_device=[baseline=std::move(baseline)](ui::UiDeviceState& device){
    device.ObserveEnvironment(baseline,ui::UiEnvironmentReason::kStartup);
  };
  ConfigureUiFactories(std::move(factories));
}
}
