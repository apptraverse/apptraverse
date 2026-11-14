#include "chat_ui_session.h"
#include <atomic>
#include <cstdlib>
#include <set>
#include <thread>
#include "apptraverse/application_model_runtime.h"
#include "apptraverse/application_ui_session.h"
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
       std::function<void()> callback,bool load,ChatSession::FlushProfile flush_transport)
    :profile{std::move(path)},name{std::move(who)},initial_host{std::move(invited)},
     host{role},load_only{load},changed{std::move(callback)},
     proxy{[this](auto work){runtime.Post(std::move(work));}},
     transport{profile/"transport",name,[this]{
       auto value=transport.View();(void)runtime.TryPost([value=std::move(value)](ae::Domain& domain)mutable{
         domain.Find(RootId()).as<ChatUi>()->AcceptTransport(std::move(value));
       });
     },{},std::move(flush_transport)}{}
  std::filesystem::path profile;
  std::string name,initial_host;
  bool host{},load_only{},started{};
  std::atomic<bool> stopping{false};
  std::function<void()> changed;
  ApplicationModelRuntime runtime;
  ModelObjectProxy proxy;
  ChatSession transport;
  std::thread worker;
  std::unique_ptr<ApplicationUiSession> ui;
  // Worker-only, valid until Run has drained and saved.
  ae::RamDomainStorage* active_user{};
  std::filesystem::path active_profile;
  using CheckpointCompletion=std::shared_ptr<std::function<void(bool)>>;
  std::vector<CheckpointCompletion> deferred_checkpoints;
  void SaveCheckpoint(ChatUi& root,CheckpointCompletion done){
    if(root.editing.sending){
      deferred_checkpoints.push_back(std::move(done));
      return;
    }
    (*done)(Persist(root,*active_user,active_profile));
  }
  void CompleteDeferredCheckpoints(ChatUi& root){
    if(deferred_checkpoints.empty())return;
    // The send callback acknowledges durable transport storage. Save this
    // settled model in the same work item, before another GUI Send can start.
    auto pending=std::move(deferred_checkpoints);
    deferred_checkpoints.clear();
    bool const saved=Persist(root,*active_user,active_profile);
    for(auto& done:pending)(*done)(saved);
  }
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
        (void)runtime.TryPost([this,revision,accepted](ae::Domain& model){
          auto* root=model.Find(RootId()).as<ChatUi>();
          root->CompleteSend(revision,accepted);
          CompleteDeferredCheckpoints(*root);
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
    std::string initial_host,std::function<void()> changed,bool load_only,
    ChatSession::FlushProfile flush_transport)
  :impl_{std::make_unique<Impl>(std::move(profile),std::move(name),host,
      std::move(initial_host),std::move(changed),load_only,std::move(flush_transport))}{}
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
  impl_->ui.reset();
}
void Session::Checkpoint(std::function<void(bool)> completion){
  auto done=std::make_shared<std::function<void(bool)>>(std::move(completion));
  if(impl_->stopping.load()||!impl_->started){(*done)(false);return;}
  // Join the GUI queue before the transport queue. Otherwise an already
  // accepted Send can reach the transport after its checkpoint and leave a
  // saved UI draft marked in-flight without the matching durable message.
  if(!impl_->runtime.TryPost([this,done](ae::Domain&){
    impl_->transport.Checkpoint([this,done](bool transport_saved){
      if(!transport_saved){(*done)(false);return;}
      if(!impl_->runtime.TryPost([this,done](ae::Domain& domain){
        auto* root=domain.Find(RootId()).as<ChatUi>();
        // A later Send can enter while the transport flush is in flight. Never
        // persist its transient sending phase without the corresponding ACK.
        impl_->SaveCheckpoint(*root,done);
      }))(*done)(false);
    });
  }))(*done)(false);
}
bool Session::ApplyPublication(Presentation& host){
  if(impl_->stopping.load())return false;
  auto bytes=impl_->runtime.TakePublishedCopy();
  if(bytes.empty())return false;
  if(!impl_->ui)
    impl_->ui=std::make_unique<ApplicationUiSession>(bytes,&host,impl_->proxy);
  else impl_->ui->ApplyStructural(bytes);
  impl_->runtime.NotifyPublicationConsumed();return true;
}
ChatUi const* Session::Mirror()const{
  return impl_->ui?&static_cast<ChatUi const&>(impl_->ui->root()):nullptr;
}
ChatView Session::TransportView()const{return impl_->transport.View();}
ModelObjectProxy& Session::Proxy(){return impl_->proxy;}
#ifdef APPTRAVERSE_CHAT_UI_PRODUCER
int ExportUiSupply(std::filesystem::path const& path){
  ae::RamDomainStorage backing;BuildBacking(backing);
  auto constants=Constants(backing);std::vector<ae::ObjId> ids{constants.begin(),constants.end()};
  return WriteEmbeddedDistilledSupplyHeader(path,"shared_chat_ui","APPTRAVERSE_SHARED_CHAT_UI_SUPPLY_H_",
      RootId(),ids,backing,CollectStoredObjectIds(backing))?0:1;
}
#endif
} // namespace apptraverse::chat::gui
