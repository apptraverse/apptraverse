#pragma once
#include "download.h"
#include "apptraverse/application_model_runtime.h"
#include "apptraverse/application_ui_session.h"
#include "apptraverse/overlay_domain_storage.h"
namespace update_lab {
struct Options {
  fs::path profile, install, store, health_file, export_file, import_file, snapshot_file;
  std::string provider, health_token, language, note, device, rename, badge;
  bool latest{false};
  int release{-1}, rate{-1};
  unsigned add_clones{0}, hold_ms{0}, auto_close_ms{0};
  bool headless{false}, broken{false}, no_action{false}, suppress_health{false}, native_smoke{false};
};
class Session {
 public:
  Session(Options, apptraverse::ApplicationModelRuntime&);
  ~Session();
  int Run(apptraverse::ApplicationPublishedCallback);
 private:
  void Submit(Command);
  void Initial();
  void Request(Intent, bool prepared = false);
  void Discover();
  void Complete(TransferResult);
  bool Report(DownloadSnapshot);
  void Project();
  void Save(bool user = true);
  void SaveDownload();
  void LoadContent();
  void AddClone();
  void Import(fs::path const&);
  Options options_;
  apptraverse::ApplicationModelRuntime& runtime_;
  ae::RamDomainStorage backing_, user_store_, local_store_, transfer_store_, scene_store_;
  apptraverse::OverlayDomainStorage overlay_;
  ae::Domain domain_, local_domain_, transfer_domain_, scene_domain_;
  UserState::ptr user_;
  DeviceState::ptr local_;
  DownloadNode::ptr download_;
  LabView::ptr view_;
  LanguagePack language_;
  bool content_ready_{false}, imported_{false}, restart_{false}, initial_done_{false};
  InstalledImage installed_;
  std::vector<unsigned> available_;
  std::string offer_;
  bool discovering_{false};
  std::unique_ptr<TransferWorker> worker_;
};
void AcknowledgeHealth(Options const&, LabView const&);
std::string InspectJson(LabView const&);
int RunNative(Options const&);
int RunHeadless(Options const&);
}
