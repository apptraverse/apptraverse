#include "chat_ui_session.h"
#include <atomic>
#include <cstdlib>
#include <set>
#include <thread>
#include "apptraverse/application_model_runtime.h"
#include "apptraverse/distill.h"
#include "apptraverse/distilled_domain_export.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/single_file_user_storage.h"
#ifndef APPTRAVERSE_CHAT_UI_PRODUCER
#include "generated/chat_ui_supply.h"
#endif
namespace apptraverse::chat::gui {
namespace {
[[noreturn]] void Fail(char const* text){WriteFatalStderr(text);std::abort();}
std::set<ae::ObjId> Constants(ae::RamDomainStorage const& backing){
  std::set<ae::ObjId> result;
  for(auto id:CollectStoredObjectIds(backing))if(backing.IsConstantObject(id))result.insert(id);
  return result;
}
bool Persist(ChatUi& root,ae::RamDomainStorage const& user,std::filesystem::path const& profile){
  ae::DomainGraph graph{root.domain,ae::GraphSerializationScope::LocalPersistent};
  auto object=ChatUi::ptr::MakeFromThis(&root);
  return graph.SaveRoot(object.cached(),root.obj_id).IsOk()&&SaveUserStorageFile(user,profile);
}
#ifdef APPTRAVERSE_CHAT_UI_PRODUCER
void BuildBacking(ae::RamDomainStorage& backing){
  ae::Domain producer{backing};auto root=BuildUi(producer);
  FinalizeDistilledGraph(*root);StabilizeDistilledPersistentSnapshot(*root);SaveDistilledRoot(*root);
}
#endif
}
struct Session::Impl {
  Impl(std::filesystem::path path,std::string who,bool role,std::string invited,
       std::function<void()> callback,bool load)
    :profile{std::move(path)},name{std::move(who)},initial_host{std::move(invited)},
     host{role},load_only{load},changed{std::move(callback)},
     proxy{[this](auto work){runtime.Post(std::move(work));}},
     transport{profile/"transport",name,[this]{
       auto value=transport.View();(void)runtime.TryPost([value=std::move(value)](ae::Domain& domain)mutable{
         domain.Find(RootId()).as<ChatUi>()->AcceptTransport(std::move(value));
       });
     }},mirror_domain{mirror_storage}{}
  std::filesystem::path profile;
  std::string name,initial_host;
  bool host{},load_only{},started{};
  std::atomic<bool> stopping{false};
  std::function<void()> changed;
  ApplicationModelRuntime runtime;
  ModelObjectProxy proxy;
  ChatSession transport;
  std::thread worker;
  ae::RamDomainStorage mirror_storage;
  ae::Domain mirror_domain;
  ae::Ptr<ae::Obj> mirror;
  // Worker-only, valid until Run has drained and saved.
  ae::RamDomainStorage* active_user{};
  std::filesystem::path active_profile;
  void Run(){
    ae::RamDomainStorage user,backing;
#ifdef APPTRAVERSE_CHAT_UI_PRODUCER
    BuildBacking(backing);
    active_profile=load_only?profile:profile/("debug-run-"+std::to_string(ae::ObjId::GenerateUnique().id()));
    bool restore=load_only;
#else
    InstallEmbeddedDistilledBacking(backing,embedded_distilled::shared_chat_ui::kEmbeddedGraph);
    active_profile=profile;bool restore=true;
#endif
    if(restore&&!LoadUserStorageFile(user,active_profile))Fail("cannot load chat UI USER\n");
    OverlayDomainStorage overlay{user,backing,Constants(backing)};ae::Domain domain{overlay};
    auto root=LoadApplication<ChatUi>(domain,RootId());active_user=&user;
    root->SetImmediateEventCompaction(true);
    root->create=[this]{transport.CreateRoom();};
    root->join=[this](std::string invite){transport.JoinRoom(std::move(invite));};
    root->send=[this](std::string text,std::uint64_t revision){
      transport.Send(std::move(text),[this,revision](bool accepted){
        (void)runtime.TryPost([revision,accepted](ae::Domain& model){
          model.Find(RootId()).as<ChatUi>()->CompleteSend(revision,accepted);
        });
      });
    };
    root->Configure(host,name,initial_host);
    root->AcceptTransport(transport.View());
    runtime.Run(domain,*root,[this](auto){
      if(stopping.load())runtime.AbandonUnconsumedPublicationsForTeardown();else changed();
    });
    if(!Persist(*root,user,active_profile))Fail("cannot save chat UI USER\n");
    active_user=nullptr;
  }
};
Session::Session(std::filesystem::path profile,std::string name,bool host,
    std::string initial_host,std::function<void()> changed,bool load_only)
  :impl_{std::make_unique<Impl>(std::move(profile),std::move(name),host,
      std::move(initial_host),std::move(changed),load_only)}{EnsureUiRegistration();}
Session::~Session(){Stop();}
void Session::Start(){
  if(impl_->started||impl_->stopping.load())return;
  impl_->started=true;impl_->worker=std::thread{[this]{impl_->Run();}};impl_->transport.Start();
}
void Session::Stop(){
  if(impl_->stopping.exchange(true)||!impl_->started)return;
  // Finish GUI inputs first; then all transport completions can enter the model
  // queue before closing acceptance and draining it for the final USER save.
  impl_->runtime.SyncPost([](ae::Domain&){});
  impl_->transport.Stop();impl_->runtime.RequestStop();
  impl_->runtime.AbandonUnconsumedPublicationsForTeardown();impl_->worker.join();
  if(impl_->mirror)UnloadPresenters(*impl_->mirror);
}
void Session::Checkpoint(std::function<void(bool)> completion){
  auto done=std::make_shared<std::function<void(bool)>>(std::move(completion));
  if(impl_->stopping.load()||!impl_->started){(*done)(false);return;}
  impl_->transport.Checkpoint([this,done](bool transport_saved){
    if(!transport_saved){(*done)(false);return;}
    if(!impl_->runtime.TryPost([this,done](ae::Domain& domain){
      auto* root=domain.Find(RootId()).as<ChatUi>();
      (*done)(Persist(*root,*impl_->active_user,impl_->active_profile));
    }))(*done)(false);
  });
}
bool Session::ApplyPublication(Presentation& host){
  if(impl_->stopping.load())return false;
  auto bytes=impl_->runtime.TakePublishedCopy();
  if(bytes.empty())return false;
  ByteSource source;source.data=bytes.data();source.size=bytes.size();
  if(!impl_->mirror){
    impl_->mirror=LoadInitialPublication(source,impl_->mirror_domain,impl_->mirror_storage);
    InitializePresenters(*impl_->mirror,&host,&impl_->proxy);
  }else ApplyStructuralPublicationAndUpdatePresenters(source,impl_->mirror_domain,
      impl_->mirror_storage,*impl_->mirror,&host,&impl_->proxy);
  impl_->runtime.NotifyPublicationConsumed();return true;
}
ChatUi const* Session::Mirror()const{return static_cast<ChatUi const*>(impl_->mirror.get());}
ModelObjectProxy& Session::Proxy(){return impl_->proxy;}
#ifdef APPTRAVERSE_CHAT_UI_PRODUCER
int ExportUiSupply(std::filesystem::path const& path){
  EnsureUiRegistration();ae::RamDomainStorage backing;BuildBacking(backing);
  auto constants=Constants(backing);std::vector<ae::ObjId> ids{constants.begin(),constants.end()};
  return WriteEmbeddedDistilledSupplyHeader(path,"shared_chat_ui","APPTRAVERSE_SHARED_CHAT_UI_SUPPLY_H_",
      RootId(),ids,backing,CollectStoredObjectIds(backing))?0:1;
}
#endif
} // namespace apptraverse::chat::gui
