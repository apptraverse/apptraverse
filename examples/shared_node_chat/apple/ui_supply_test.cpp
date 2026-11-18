#include "AppleComposition.h"
#include "apptraverse/application_ui_session.h"
#include "apptraverse/distill.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/overlay_domain_storage.h"
#include APPTRAVERSE_CHAT_UI_SUPPLY_HEADER
#include <array>
#include <cstdlib>
#include <iostream>
using namespace apptraverse;
using namespace apptraverse::chat::gui;
#define CHECK(x) do { if(!(x)){std::cerr<<"FAIL " #x<<" line "<<__LINE__<<'\n';std::abort();} }while(false)
struct Host final:Presentation {
  bool window{};
  std::array<bool,static_cast<unsigned>(Control::Count)> controls{};
  void PresentWindow(ChatUi const&)override{window=true;}
  void RemoveWindow()override{window=false;}
  void Present(Control c,ControlState const&,ChatUi const&)override{CHECK(window);controls.at(static_cast<unsigned>(c))=true;}
  void Remove(Control)override{}
};
int main(){
  ConfigureAppleComposition({});
  ae::RamDomainStorage user,local,backing;
  auto const& supply=embedded_distilled::shared_chat_ui::kEmbeddedGraph;
  InstallEmbeddedDistilledBacking(backing,supply);
  ClientLocalDomainStorage routed{user,local,backing,CollectClientLocalRoots(backing)};
  OverlayDomainStorage overlay{routed,backing,EmbeddedSupplyConstantIdSet(supply)};
  ae::Domain model{overlay};auto root=LoadApplication<ChatUi>(model,RootId());
  CHECK(root->native_presenters.size()==static_cast<unsigned>(Control::Count));
  CHECK(root->device_state&&root->layout_state);
  root->Configure(false,"Native supply test","");
  CHECK(root->Title()=="SharedNode Chat - Native supply test");
  ByteSink bytes;SerializeInitialPublication(*root,bytes);
  ModelObjectProxy proxy{[](auto){CHECK(false);}};Host host;
  { ApplicationUiSession mirror{bytes.bytes,&host,proxy};
    CHECK(host.window);
    for(bool presented:host.controls)CHECK(presented);
    CHECK(&mirror.root()!=&*root);
  }
  CHECK(!host.window);
  std::cout<<"PASS Apple consumer supply: LOCAL routing, typed graph, all 12 native presenter projections and teardown\n";
}
