#include "chat_ui_session.h"
#include <atomic>
#include <cstdlib>
#include <set>
#include <thread>
#include "apptraverse/application_model_runtime.h"
#include "apptraverse/application_ui_session.h"
#include "apptraverse/distill.h"
#include "apptraverse/distilled_domain_export.h"
#include "apptraverse/embedded_application_model_session.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/single_file_user_storage.h"
#ifndef APPTRAVERSE_CHAT_UI_PRODUCER
#  ifdef APPTRAVERSE_CHAT_UI_SUPPLY_HEADER
#    include APPTRAVERSE_CHAT_UI_SUPPLY_HEADER
#  else
#    include "generated/chat_ui_supply.h"
#  endif
#endif
namespace apptraverse::chat::gui {
namespace {
[[noreturn]] void Fail(char const* text) {
  WriteFatalStderr(text);
  std::abort();
}
std::set<ae::ObjId> Constants(ae::RamDomainStorage const& backing) {
  std::set<ae::ObjId> result;
  for (auto id : CollectStoredObjectIds(backing))
    if (backing.IsConstantObject(id)) result.insert(id);
  return result;
}
bool PersistLocal(ChatUi& root, ae::RamDomainStorage const& local,
                  std::filesystem::path const& profile) {
  // Only the ClientLocalState ownership graph reaches disk. Chat connection,
  // drafts, history and delivery receipts are intentionally session RAM.
  ae::DomainGraph graph{root.domain,
                        ae::GraphSerializationScope::LocalPersistent};
  return graph.SaveRoot(root.device_state.cached(), root.device_state.id())
             .IsOk() &&
         SaveUserStorageFile(local, profile / "local");
}
#ifdef APPTRAVERSE_CHAT_UI_PRODUCER
void BuildBacking(ae::RamDomainStorage& backing) {
  ae::Domain producer{backing};
  auto root = BuildUi(producer);
  FinalizeDistilledGraph(*root);
  StabilizeDistilledPersistentSnapshot(*root);
  SaveDistilledRoot(*root);
}
#endif
}  // namespace
struct Session::Impl {
  Impl(std::filesystem::path path, std::string who, bool role,
       std::string invited, std::function<void()> callback, bool load,
       ChatSession::FlushProfile flush_transport,
       std::filesystem::path transport_profile)
      : profile{std::move(path)},
        name{std::move(who)},
        initial_host{std::move(invited)},
        host{role},
        load_only{load},
        changed{std::move(callback)},
        proxy{[this](auto work) { runtime.Post(std::move(work)); }},
        transport{transport_profile.empty() ? profile / "transport"
                                            : std::move(transport_profile),
                  name,
                  [this] {
                    auto value = transport.View();
                    (void)runtime.TryPost(
                        [value = std::move(value)](ae::Domain& domain) mutable {
                          domain.Find(RootId()).as<ChatUi>()->AcceptTransport(
                              std::move(value));
                        });
                  },
                  {},
                  std::move(flush_transport)} {}
  std::filesystem::path profile;
  std::string name, initial_host;
  bool host{}, load_only{}, started{};
  std::atomic<bool> stopping{false};
  std::function<void()> changed;
  ApplicationModelRuntime runtime;
  ModelObjectProxy proxy;
  ChatSession transport;
  std::thread worker;
  std::unique_ptr<ApplicationUiSession> ui;
  // Worker-only LOCAL storage. No chat USER file is loaded or saved.
  ae::RamDomainStorage* active_local{};
  std::filesystem::path active_profile;
  void Run() {
    ae::RamDomainStorage user, local, backing;
#ifdef APPTRAVERSE_CHAT_UI_PRODUCER
    BuildBacking(backing);
    active_profile =
        load_only
            ? profile
            : profile / ("debug-run-" +
                         std::to_string(ae::ObjId::GenerateUnique().id()));
    bool restore = load_only;
#else
    InstallEmbeddedDistilledBacking(
        backing, embedded_distilled::shared_chat_ui::kEmbeddedGraph);
    active_profile = profile;
    bool restore = true;
#endif
    if (restore && !LoadUserStorageFile(local, active_profile / "local"))
      Fail("cannot load chat UI LOCAL\n");
    active_local = &local;
    ApplicationModelSessionHooks<ChatUi> hooks;
    hooks.prepare = [this](ChatUi& model) {
      auto root = ChatUi::ptr::MakeFromThis(&model);
      root->device_state.Load();
      PresentationFactories().prepare_device(*root->device_state);
      root->SetImmediateEventCompaction(true);

      root->create = [this] { transport.CreateRoom(); };
      root->join = [this](std::string invite) {
        transport.JoinRoom(std::move(invite));
      };
      root->send = [this](std::string text, std::uint64_t revision) {
        transport.Send(std::move(text), [this, revision](bool accepted) {
          if (accepted) transport.NotifyPushPeers();
          (void)runtime.TryPost([this, revision, accepted](ae::Domain& model) {
            auto* root = model.Find(RootId()).as<ChatUi>();
            root->CompleteSend(revision, accepted);
          });
        });
      };
      root->Configure(host, name, initial_host);
      root->AcceptTransport(transport.View());
    };
    hooks.persist = [this, &local](ChatUi& root) {
      return PersistLocal(root, local, active_profile) ? 0 : 1;
    };
    int result = RunClientLocalApplicationModelSession(
        user, local, backing, Constants(backing), RootId(), runtime,
        [this](auto) {
          if (stopping.load())
            runtime.AbandonUnconsumedPublicationsForTeardown();
          else
            changed();
        },
        hooks);
    if (result != 0) Fail("cannot save chat UI LOCAL\n");
    active_local = nullptr;
  }
};
Session::Session(std::filesystem::path profile, std::string name, bool host,
                 std::string initial_host, std::function<void()> changed,
                 bool load_only, ChatSession::FlushProfile flush_transport,
                 std::filesystem::path transport_profile)
    : impl_{std::make_unique<Impl>(std::move(profile), std::move(name), host,
                                   std::move(initial_host), std::move(changed),
                                   load_only, std::move(flush_transport),
                                   std::move(transport_profile))} {}
Session::~Session() { Stop(); }
void Session::ConfigurePush(std::string uid) {
  impl_->transport.ConfigurePush(std::move(uid));
}
void Session::RegisterPushToken(std::string token, bool apple) {
  impl_->transport.RegisterPushToken(std::move(token), apple);
}
void Session::RegisterWebPush(std::string endpoint) {
  impl_->transport.RegisterWebPush(std::move(endpoint));
}
void Session::Start() {
  if (impl_->started || impl_->stopping.load()) return;
#if defined(_WIN32)
  if (auto uid = std::getenv("APPTRAVERSE_PUSH_SERVICE_UID"))
    ConfigurePush(uid);
#endif
  EnsureUiRegistration();
  impl_->started = true;
  impl_->worker = std::thread{[this] { impl_->Run(); }};
  impl_->transport.Start();
}
void Session::Stop() {
  if (impl_->stopping.exchange(true) || !impl_->started) return;
  // Finish GUI inputs first; then all transport completions can enter the model
  // queue before closing acceptance and draining it for the final LOCAL save.
  impl_->runtime.SyncPost([](ae::Domain&) {});
  impl_->transport.Stop();
  impl_->runtime.RequestStop();
  impl_->runtime.AbandonUnconsumedPublicationsForTeardown();
  impl_->worker.join();
  impl_->ui.reset();
}
void Session::Checkpoint(std::function<void(bool)> completion) {
  auto done =
      std::make_shared<std::function<void(bool)>>(std::move(completion));
  if (impl_->stopping.load() || !impl_->started) {
    (*done)(false);
    return;
  }
  if (!impl_->runtime.TryPost([this, done](ae::Domain& domain) {
        auto* root = domain.Find(RootId()).as<ChatUi>();
        (*done)(
            PersistLocal(*root, *impl_->active_local, impl_->active_profile));
      }))
    (*done)(false);
}
bool Session::ApplyPublication(Presentation& host) {
  return ApplyPublication(static_cast<void*>(&host));
}
bool Session::ApplyPublication(void* host) {
  if (impl_->stopping.load()) return false;

  auto bytes = impl_->runtime.TakePublishedCopy();
  if (bytes.empty()) return false;
  if (!impl_->ui)
    impl_->ui =
        std::make_unique<ApplicationUiSession>(bytes, host, impl_->proxy);
  else
    impl_->ui->ApplyStructural(bytes);
  impl_->runtime.NotifyPublicationConsumed();

  return true;
}
void Session::RemountPresentation() {
  if (impl_->ui && !impl_->stopping.load()) impl_->ui->RemountPresentation();
}
bool Session::CaptureUser(std::vector<std::uint8_t>& bytes) {
  // Compatibility for old lifecycle adapters: a portable chat USER is empty.
  ae::RamDomainStorage empty;
  return SerializeUserStorage(empty, bytes);
}
bool Session::CaptureLocal(std::vector<std::uint8_t>& bytes,
                           std::filesystem::path* output_file) {
  if (!impl_->ui || impl_->stopping.load()) return false;
  bool saved = false;
  impl_->runtime.SyncPost([&](ae::Domain& domain) {
    auto* root = domain.Find(RootId()).as<ChatUi>();
    ae::DomainGraph graph{root->domain,
                          ae::GraphSerializationScope::LocalPersistent};
    saved = graph.SaveRoot(root->device_state.cached(), root->device_state.id())
                .IsOk() &&
            SerializeUserStorage(*impl_->active_local, bytes);
    if (saved && output_file)
      *output_file = impl_->active_profile / "local" / "user.bin";
  });
  return saved;
}
ChatUi const* Session::Mirror() const {
  return impl_->ui ? &static_cast<ChatUi const&>(impl_->ui->root()) : nullptr;
}
ChatView Session::TransportView() const { return impl_->transport.View(); }
ModelObjectProxy& Session::Proxy() { return impl_->proxy; }
#ifdef APPTRAVERSE_CHAT_UI_PRODUCER
int ExportUiSupply(std::filesystem::path const& path) {
  ae::RamDomainStorage backing;
  BuildBacking(backing);
  auto constants = Constants(backing);
  std::vector<ae::ObjId> ids{constants.begin(), constants.end()};
  return WriteEmbeddedDistilledSupplyHeader(
             path, "shared_chat_ui", "APPTRAVERSE_SHARED_CHAT_UI_SUPPLY_H_",
             RootId(), ids, backing, CollectStoredObjectIds(backing))
             ? 0
             : 1;
}
#endif
}  // namespace apptraverse::chat::gui
