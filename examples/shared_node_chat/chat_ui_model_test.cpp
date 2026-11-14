#include "chat_ui_model.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include "apptraverse/distill.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/single_file_user_storage.h"
#include "apptraverse/application_ui_session.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "generated/chat_ui_supply.h"
using namespace apptraverse;
using namespace apptraverse::chat::gui;
#define CHECK(x) do{if(!(x)){std::cerr<<"check " #x " line "<<__LINE__<<'\n';std::abort();}}while(false)
struct Host : Presentation {
  unsigned loaded{}, removed{};
  bool window{};
  void PresentWindow(ChatUi const&)override{window=true;}
  void RemoveWindow()override{CHECK(removed==static_cast<unsigned>(Control::Count));window=false;}
  void Present(Control,ControlState const&,ChatUi const&)override{CHECK(window);++loaded;}
  void Remove(Control)override{++removed;}
};
int main(){
  EnableNoninteractiveCrt();EnsureUiRegistration();
  ae::RamDomainStorage storage;ae::Domain domain{storage};
  auto app=BuildUi(domain);FinalizeDistilledGraph(*app);
  app->SetImmediateEventCompaction(true);
  app->Configure(true,"Host","");
  app->Configure(false,"Replacement", "unexpected-host");
  CHECK(app->editing.host&&app->editing.name=="Host"&&app->editing.invite.empty());
  unsigned creates=0,sends=0;std::uint64_t revision=0;
  app->create=[&]{++creates;};app->join=[](std::string){CHECK(false);};
  app->send=[&](std::string text,std::uint64_t r){CHECK(text=="first");++sends;revision=r;};
  chat::ChatView network;network.ready=true;
  app->AcceptTransport(network);CHECK(creates==1);
  network.in_room=true;app->AcceptTransport(network);
  app->EditDraft("first");app->Send();CHECK(sends==1&&app->editing.sending);
  app->CompleteSend(revision,false);CHECK(app->editing.draft=="first");
  app->Send();app->EditDraft("newer");app->CompleteSend(revision,true);
  CHECK(app->editing.draft=="newer"&&!app->editing.sending);
  app->send=[&](std::string text,std::uint64_t r){CHECK(text=="newer");revision=r;};
  app->Send();app->CompleteSend(revision,true);CHECK(app->editing.draft.empty());
  app->EditDraft(std::string(8193,'x'));app->Send();
  CHECK(app->editing.error_label==static_cast<int>(Label::TooLong));
  app->EditDraft("retained");
  app->SubmitFrameFromNative(-80,110,900,600,true);
  app->Resize(320,500);
  CHECK(app->client_width()==320&&app->restore_client_width()==900);
  CHECK(app->HasExplicitPosition()&&app->x()==-80&&app->y()==110);
  app->Resize(900,600);
  ByteSink publication;SerializeInitialPublication(*app,publication);
  ModelObjectProxy proxy{[](auto){}};Host host;
  {
    ApplicationUiSession ui_session{publication.bytes,&host,proxy};
    auto* ui=static_cast<ChatUi*>(&ui_session.root());CHECK(ui!=&*app);
    CHECK(ui->editing.draft=="retained"&&ui->client_width()==900);
    CHECK(ui->x()==-80&&ui->y()==110);
    CHECK(&*ui->presenter->window==static_cast<ui::UiWindow*>(ui));
    // Standard activation calls OnLoad, then synchronizes OnModelChanged once.
    CHECK(host.loaded==2*static_cast<unsigned>(Control::Count));
  }
  CHECK(!host.window&&host.removed==static_cast<unsigned>(Control::Count));
  SaveDistilledRoot(*app);
  std::vector<std::uint8_t> saved;CHECK(SerializeUserStorage(storage,saved));
  ae::RamDomainStorage cold;CHECK(DeserializeUserStorage(cold,saved));
  ae::Domain cold_domain{cold};auto restored=LoadApplication<ChatUi>(cold_domain,RootId());
  CHECK(restored->editing.draft=="retained"&&restored->client_width()==900);
  CHECK(restored->HasExplicitPosition()&&restored->x()==-80&&restored->y()==110);
  restored->Configure(false,"Replacement","unexpected-host");
  CHECK(restored->editing.host&&restored->editing.name=="Host");
  CHECK(restored->presenters.size()==static_cast<unsigned>(Control::Count));
  CHECK(&*restored->presenters.front()->model==&*restored);
  {
    // Consume the actual executable-emitted supply; no builder initializes this graph.
    ae::RamDomainStorage embedded_user,embedded_backing;
    auto const& supply=embedded_distilled::shared_chat_ui::kEmbeddedGraph;
    InstallEmbeddedDistilledBacking(embedded_backing,supply);
    OverlayDomainStorage overlay{embedded_user,embedded_backing,EmbeddedSupplyConstantIdSet(supply)};
    ae::Domain consumer{overlay};
    auto loaded=LoadApplication<ChatUi>(consumer,RootId());
    CHECK(!loaded->editing.configured&&loaded->client_width()==840);
    CHECK(loaded->Text(Label::SelectAll)=="Select All");
    CHECK(&*loaded->presenter->window==static_cast<apptraverse::ui::UiWindow*>(&*loaded));
    CHECK(&*loaded->presenters.front()->model==&*loaded);
    loaded->Configure(true,"Embedded","");
    CHECK(loaded->Title()=="SharedNode Chat - Embedded");
  }
  std::cout<<"Chat UI model PASS: send ACK revisions, validation, mirror/presenters, current restart\n";
}
