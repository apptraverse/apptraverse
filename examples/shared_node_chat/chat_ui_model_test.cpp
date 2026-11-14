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
using namespace apptraverse;
using namespace apptraverse::chat::gui;
#define CHECK(x) do{if(!(x)){std::cerr<<"check " #x " line "<<__LINE__<<'\n';std::abort();}}while(false)
struct Host : Presentation {
  unsigned loaded{}, removed{};
  void Present(Control,ControlState const&,ChatUi const&)override{++loaded;}
  void Remove(Control)override{++removed;}
};
int main(){
  EnableNoninteractiveCrt();EnsureUiRegistration();
  ae::RamDomainStorage storage;ae::Domain domain{storage};
  auto app=BuildUi(domain);FinalizeDistilledGraph(*app);
  app->SetImmediateEventCompaction(true);
  app->Configure(true,"Host","");
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
  app->EditDraft("retained");app->Resize(900,600);
  ByteSink publication;SerializeInitialPublication(*app,publication);
  ae::RamDomainStorage ui_storage;ae::Domain ui_domain{ui_storage};
  ByteSource source;source.data=publication.bytes.data();source.size=publication.bytes.size();
  auto mirror=LoadInitialPublication(source,ui_domain,ui_storage);
  auto* ui=static_cast<ChatUi*>(mirror.get());CHECK(ui!=&*app);
  CHECK(ui->editing.draft=="retained"&&ui->layout.width==900);
  ModelObjectProxy proxy{[](auto){}};Host host;
  InitializePresenters(*ui,&host,&proxy);
  // Standard activation calls OnLoad, then synchronizes OnModelChanged once.
  CHECK(host.loaded==2*static_cast<unsigned>(Control::Count));
  UnloadPresenters(*ui);CHECK(host.removed==static_cast<unsigned>(Control::Count));
  SaveDistilledRoot(*app);
  std::vector<std::uint8_t> saved;CHECK(SerializeUserStorage(storage,saved));
  ae::RamDomainStorage cold;CHECK(DeserializeUserStorage(cold,saved));
  ae::Domain cold_domain{cold};auto restored=LoadApplication<ChatUi>(cold_domain,RootId());
  CHECK(restored->editing.draft=="retained"&&restored->layout.width==900);
  CHECK(restored->presenters.size()==static_cast<unsigned>(Control::Count));
  CHECK(&*restored->presenters.front()->model==&*restored);
  std::cout<<"Chat UI model PASS: send ACK revisions, validation, mirror/presenters, current restart\n";
}
