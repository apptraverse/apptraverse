#include "session.h"
#include "install.h"
#include "seed.h"
#include "apptraverse/distill.h"
#include "apptraverse/runtime_node.h"
#include <algorithm>
#include <iostream>
namespace update_lab {
namespace {
void SaveNative(ae::RamDomainStorage const& storage, fs::path const& profile) {
  Bytes bytes;
  if (!apptraverse::SerializeUserStorage(storage, bytes) || !AtomicWrite(profile / "user.bin", bytes)) Fail("native state save failed");
}
std::string Quote(std::string const& text) {
  std::string out{"\""};
  for (unsigned char c : text) {
    if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if (c >= 32) out += static_cast<char>(c);
  }
  return out + '"';
}
}
Session::Session(Options options, apptraverse::ApplicationModelRuntime& runtime)
    : options_{std::move(options)}, runtime_{runtime},
      overlay_{user_store_, backing_, {kPrefabId, kSupplyId}}, domain_{overlay_},
      local_domain_{local_store_}, transfer_domain_{transfer_store_}, scene_domain_{scene_store_} {
  scene_domain_.set_parent_domain(&transfer_domain_);
  unsigned version = 0;
  if (!ApplyStatePackage(Bytes{std::begin(kSeedSupply), std::end(kSeedSupply)}, backing_, version)) Fail("embedded supply load failed");
  Release seed;
  if (!Decode(Bytes{std::begin(kSeedRelease), std::end(kSeedRelease)}, seed) ||
      !ReadLanguage(backing_, *FindLanguage(seed, "en"), language_)) Fail("embedded language load failed");
  auto cache = Cache(options_.profile);
  if (!fs::exists(cache / FileId(seed.supply)) &&
      !AtomicWrite(cache / FileId(seed.supply), Bytes{std::begin(kSeedSupply), std::end(kSeedSupply)})) Fail("seed cache write failed");
  if (!ReadInstalled(options_.profile, installed_)) {
    installed_ = {seed, {}};
    if (!WriteInstalled(options_.profile, installed_)) Fail("cannot initialize installed image");
  }
  available_ = {installed_.release.id};
  if (!apptraverse::LoadUserStorageFile(user_store_, options_.profile) ||
      !apptraverse::LoadUserStorageFile(local_store_, options_.profile / "local") ||
      !apptraverse::LoadUserStorageFile(transfer_store_, options_.profile / "local" / "transfer")) Fail("profile read failed");
  if (user_store_.state.empty()) {
    user_ = UserState::ptr::Create(ae::CreateWith{domain_}.with_id(kUserId));
    user_->language = "en"; user_->note = ""; user_->release = installed_.release.id; user_->clones = {};
    auto clone = ClonePrefab(domain_); clone->name = "A"; user_->clones.push_back(clone);
    clone = ClonePrefab(domain_); clone->name = "B"; user_->clones.push_back(clone);
    apptraverse::InitializeRuntimeNode(*user_);
  } else user_ = apptraverse::LoadApplication<UserState>(domain_, kUserId);
  if (local_store_.state.empty()) {
    local_ = DeviceState::ptr::Create(ae::CreateWith{local_domain_}.with_id(kLocalId));
    local_->device = options_.device.empty() ? Platform() : options_.device;
    local_->x = 160; local_->y = 120; local_->width = 820; local_->height = 720; local_->bytes_per_second = 0;
    apptraverse::InitializeRuntimeNode(*local_);
  } else local_ = apptraverse::LoadApplication<DeviceState>(local_domain_, kLocalId);
  if (transfer_store_.state.empty()) {
    download_ = DownloadNode::ptr::Create(ae::CreateWith{transfer_domain_}.with_id(kDownloadId));
    download_->state = {0, {0, "en", false}, "idle", "", {}, {}};
    apptraverse::InitializeRuntimeNode(*download_);
  } else download_ = apptraverse::LoadApplication<DownloadNode>(transfer_domain_, kDownloadId);
  user_->SetImmediateEventCompaction(true); local_->SetImmediateEventCompaction(true); download_->SetImmediateEventCompaction(true);
  if (fs::exists(options_.profile / "local" / "rolled-back")) {
    auto state = download_->state; ++state.epoch; state.phase = "rolled-back";
    state.message = "Executable rollback: previous USER and LOCAL restored";
    state.intent.broken = false; download_->Change(state); SaveDownload();
    std::error_code ec; fs::remove(options_.profile / "local" / "rolled-back", ec);
  }
  view_ = LabView::ptr::Create(ae::CreateWith{scene_domain_}.with_id(kViewId));
  view_->download = download_;
  view_->submit = [this](Command c) { Submit(std::move(c)); };
  view_->data = {{}, {}, {}, {}, "", "", "", "", 0, UPDATE_LAB_EXECUTABLE, 0, 0, 820, 720, 0, false, false, false};
  // This root is a runtime projection, not a persisted history owner.
  // A crash can separate the two native file replacements. Finish the durable
  // decision before the first publication, including an offline trial boot.
  auto const& transfer = download_->state;
  if (transfer.phase == "ready" || transfer.phase == "staged") {
    Bytes bytes;
    UpdatePlan prepared{};
    if (ReadFile(options_.profile / "local" / "prepared-plan.bin", bytes) &&
        Decode(bytes, prepared) && prepared.executable == UPDATE_LAB_EXECUTABLE &&
        prepared.intent.release == transfer.intent.release && prepared.intent.language == transfer.intent.language) {
      TransferResult recovered{transfer.epoch, prepared.intent, std::move(prepared), {}, 0, false, {}};
      recovered.resumed_bytes = recovered.plan.bytes;
      Complete(std::move(recovered));
      return;
    }
  }
  LoadContent(); Project();
}
Session::~Session() = default;
void Session::SaveDownload() { download_.Save(); SaveNative(transfer_store_, options_.profile / "local" / "transfer"); }
void Session::Save(bool save_user) {
  if (save_user && !imported_) { user_.Save(); SaveNative(user_store_, options_.profile); }
  local_.Save(); SaveNative(local_store_, options_.profile / "local");
  SaveDownload();
}
void Session::LoadContent() {
  auto const& image = installed_.release;
  content_ready_ = false;
  if (image.id != user_->release || image.executable != UPDATE_LAB_EXECUTABLE) return;
  auto resource = FindLanguage(image, user_->language);
  if (!resource) return;
  auto cache = Cache(options_.profile);
  Bytes bytes;
  unsigned version = 0;
  if (!ReadFile(cache / FileId(image.supply), bytes) || !ApplyStatePackage(bytes, backing_, version) || version != image.id) return;
  ae::RamDomainStorage downloaded;
  if (!resource->full.empty()) {
    unsigned revision = 0;
    if (!installed_.resources.contains(resource->locale) ||
        !ReadFile(cache / FileId(resource->full), bytes) || !ApplyStatePackage(bytes, downloaded, revision) ||
        revision != resource->revision) return;
  }
  if (!ReadLanguage(resource->full.empty() ? backing_ : downloaded, *resource, language_)) return;
#if UPDATE_LAB_SCHEMA
  for (auto const& c : user_->clones) {
    auto clone = c.Load();
    clone->owns_badge = user_store_.Load({c.id(), TextObject::kClassId, 1}).result == ae::DomainLoadResult::kLoaded;
    if (backing_.Load({kPrefabId, TextObject::kClassId, 1}).result == ae::DomainLoadResult::kLoaded) {
      ae::DomainGraph graph{&domain_};
      auto result = graph.LoadVersion(ae::Version<1>{}, *clone, c.id());
      if (result.IsErr()) Fail("S1 origin load failed");
    }
  }
#endif
  content_ready_ = true;
}
void Session::Project() {
  auto& data = view_->data;
  data.text = language_.text;
  auto definition = SupplyDefinition::ptr::Declare(ae::CreateWith{domain_}.with_id(kSupplyId));
  data.languages = definition.Load()->languages;
  data.language = content_ready_ ? user_->language : "";
  data.note = user_->note; data.device = local_->device;
  data.release = user_->release; data.executable = UPDATE_LAB_EXECUTABLE;
  data.x = local_->x; data.y = local_->y; data.width = local_->width; data.height = local_->height;
  data.rate = local_->bytes_per_second; data.ready = content_ready_; data.restarting = restart_; data.commands_done = initial_done_ && !discovering_;
  data.distribution.releases = available_; data.distribution.offer = offer_; data.distribution.installed = installed_.resources;
  data.clones.clear();
  data.clone_ids.clear();
  for (auto const& c : user_->clones) {
    data.clone_ids.push_back(c.id().id());
    auto clone = c.Load();
    std::string value = clone->name;
#if UPDATE_LAB_SCHEMA
    if (content_ready_ && backing_.Load({kPrefabId, TextObject::kClassId, 1}).result == ae::DomainLoadResult::kLoaded) value += "  [" + clone->badge + "]";
#endif
    data.clones.push_back(value);
  }
  data.detail = download_->state.phase + ": " + download_->state.message;
  view_->Changed();
}
void Session::AddClone() {
  auto clone = ClonePrefab(domain_);
  clone->name = std::to_string(user_->clones.size() + 1);
  user_->Change(2, "", 0, clone);
}
void Session::Initial() {
  if (initial_done_) return;
  initial_done_ = true;
  bool requested = false;
  if (!options_.no_action) {
    if (!options_.import_file.empty()) { Import(options_.import_file); return; }
    if (options_.rate >= 0) local_->Change(local_->x, local_->y, local_->width, local_->height, static_cast<unsigned>(options_.rate));
    if (!options_.note.empty()) user_->Change(1, options_.note);
    for (unsigned n = 0; n < options_.add_clones; ++n) AddClone();
    if (!options_.rename.empty()) user_->Change(3, options_.rename, 0);
#if UPDATE_LAB_SCHEMA
    if (!options_.badge.empty()) user_->Change(4, options_.badge, 0);
#endif
    if (options_.latest || options_.release >= 0 || !options_.language.empty() || options_.broken) {
      unsigned release = options_.release >= 0 ? static_cast<unsigned>(options_.release) : user_->release;
      std::string language = options_.language.empty() ? user_->language : options_.language;
      if (options_.latest) release = kLatest;
      Request({release, language, options_.broken});
      requested = true;
    }
  }
  if (!requested && (download_->state.phase == "queued" || download_->state.phase == "catalog" ||
      download_->state.phase == "progress" || download_->state.phase == "ready" || download_->state.phase == "staged" || download_->state.phase == "error")) {
    Request(download_->state.intent, download_->state.phase == "staged" || download_->state.phase == "ready");
  } else if (!requested && !content_ready_) Request({user_->release, user_->language, false});
  else if (!requested && !options_.no_action && (!options_.store.empty() || !options_.provider.empty())) Discover();
  if (!options_.export_file.empty() && content_ready_ &&
      (download_->state.phase == "idle" || download_->state.phase == "activated" || download_->state.phase == "rolled-back")) {
    Save(); if (!AtomicWrite(options_.export_file, FullState(user_store_, user_->release))) Fail("export failed");
  }
  Project();
}
void Session::Discover() {
  discovering_ = true;
  auto state = download_->state;
  state.intent = {kLatest, user_->language, false};
  worker_->Begin({state, installed_, UPDATE_LAB_EXECUTABLE, 0, true, false});
}
void Session::Request(Intent intent, bool prepared) {
  discovering_ = false;
  offer_.clear();
  auto state = download_->state; ++state.epoch; state.intent = std::move(intent);
  if (!prepared) state.phase = "queued";
  state.message.clear(); state.requested.clear();
  download_->Change(state); SaveDownload();
  worker_->Begin({state, installed_, UPDATE_LAB_EXECUTABLE, local_->bytes_per_second, false, prepared});
  Project();
}
bool Session::Report(DownloadSnapshot state) {
  bool accepted = false;
  runtime_.SyncPost([&, state = std::move(state)](ae::Domain&) {
    if (state.epoch != download_->state.epoch) return;
    download_->Change(state); SaveDownload(); Project(); accepted = true;
  });
  return accepted;
}
void Session::Complete(TransferResult result) {
  if (result.epoch != download_->state.epoch) return;
  if (result.preview) {
    discovering_ = false;
    if (result.error.empty()) {
      available_ = result.plan.available;
      offer_ = "R" + std::to_string(result.plan.intent.release) + " / E" + std::to_string(result.plan.executable)
        + (result.plan.executable_id.empty() ? " · " : " · EXE · ") + std::to_string(result.plan.remaining) + " B";
    } else offer_ = result.error;
    Project(); return;
  }
  available_ = result.plan.available;
  if (result.plan.executable != UPDATE_LAB_EXECUTABLE) {
    Save();
    if (options_.install.empty() || !StageExecutable(options_.install, options_.profile, result.executable, result.plan.executable)) {
      auto state = download_->state; state.phase = "error"; state.message = "Start with update_lab_launcher to install an executable";
      download_->Change(state); SaveDownload(); Project(); return;
    }
    auto state = download_->state; state.phase = "staged";
    download_->Change(state); SaveDownload(); restart_ = true; Project(); return;
  }
  // The persisted ready intent and prepared plan replay an interrupted activation.
  installed_ = result.plan.image;
  if (!WriteInstalled(options_.profile, installed_)) Fail("installed inventory save failed");
  user_->Change(0, result.intent.language, result.intent.release);
  LoadContent();
  if (!content_ready_) Fail("prepared image activation failed");
  Save();
  auto state = download_->state; state.phase = "activated";
  state.message = "resumed=" + std::to_string(result.resumed_bytes) + (result.plan.supply.steps.empty() ? "; current" : result.plan.supply.base.empty() ? "; full" : "; delta");
  download_->Change(state); SaveDownload(); Project();
  if (!options_.export_file.empty() && !AtomicWrite(options_.export_file, FullState(user_store_, user_->release))) Fail("export failed");
}
void Session::Import(fs::path const& path) {
  Bytes bytes; ae::RamDomainStorage imported; unsigned version = 0;
  if (!ReadFile(path, bytes) || !ApplyStatePackage(bytes, imported, version) || !imported.state.contains(kUserId)) Fail("USER import failed");
  auto state = download_->state;
  ++state.epoch; state.phase = "idle"; state.message.clear(); state.requested.clear();
  download_->Change(state);
  Save(false); SaveNative(imported, options_.profile);
  imported_ = true; restart_ = true; Project();
}
void Session::Submit(Command command) {
  if (command.action == "initial") { Initial(); return; }
  if (command.action == "language") Request({user_->release, command.text, false});
  else if (command.action == "release") {
    unsigned release = static_cast<unsigned>(command.a);
    Request({release, user_->language, false});
  } else if (command.action == "broken") Request({user_->release, user_->language, true});
  else if (command.action == "note") user_->Change(1, command.text);
  else if (command.action == "clone") AddClone();
  else if (command.action == "frame") local_->Change(command.a, command.b, command.c, command.d, local_->bytes_per_second);
  else if (command.action == "rate") {
    local_->Change(local_->x, local_->y, local_->width, local_->height, static_cast<unsigned>(command.a));
    if (download_->state.phase == "progress" || download_->state.phase == "catalog") Request(download_->state.intent);
  } else if (command.action == "export") {
    Save(); if (!AtomicWrite(fs::u8path(command.text), FullState(user_store_, user_->release))) Fail("USER export failed");
  } else if (command.action == "import") { Import(fs::u8path(command.text)); return; }
  Project();
}
int Session::Run(apptraverse::ApplicationPublishedCallback published) {
  worker_ = std::make_unique<TransferWorker>(options_.profile, options_.store, options_.provider,
      [this](DownloadSnapshot s) { return Report(std::move(s)); },
      [this](TransferResult result) { runtime_.Post([this, result = std::move(result)](ae::Domain&) mutable { Complete(std::move(result)); }); });
  runtime_.Run(scene_domain_, *view_, std::move(published));
  worker_->Stop(); Save(); return restart_ ? 42 : download_->state.phase == "error" ? 2 : 0;
}
void AcknowledgeHealth(Options const& options, LabView const& view) {
  auto const& phase = view.download->state.phase;
  if (options.suppress_health || options.health_file.empty() || !view.data.ready ||
      !view.data.commands_done || view.data.restarting ||
      (phase != "idle" && phase != "activated" && phase != "rolled-back")) return;
  if (!fs::exists(options.health_file) && !AtomicWrite(options.health_file, Bytes{options.health_token.begin(), options.health_token.end()})) Fail("health acknowledgement write failed");
}
std::string InspectJson(LabView const& view) {
  auto const& d = view.data; auto const& state = view.download->state;
  std::string result = "{\"release\":" + std::to_string(d.release) + ",\"executable\":" + std::to_string(d.executable) +
      ",\"language\":" + Quote(d.language) + ",\"note\":" + Quote(d.note) + ",\"device\":" + Quote(d.device) +
      ",\"rate\":" + std::to_string(d.rate) + ",\"greeting\":" + Quote(d.text[Greeting]) + ",\"offer\":" + Quote(d.distribution.offer) +
      ",\"ready\":" + (d.ready ? "true" : "false") + ",\"phase\":" + Quote(state.phase) + ",\"message\":" + Quote(state.message) + ",\"clones\":[";
  for (std::size_t i = 0; i < d.clones.size(); ++i) { if (i) result += ','; result += Quote(d.clones[i]); }
  result += "],\"clone_ids\":[";
  for (std::size_t i = 0; i < d.clone_ids.size(); ++i) { if(i) result += ','; result += std::to_string(d.clone_ids[i]); }
  result += "],\"requested\":[";
  for (std::size_t i = 0; i < state.requested.size(); ++i) {
    if (i) result += ',';
    result += Quote(state.requested[i]);
  }
  result += "],\"installed\":{";
  bool first = true;
  for (auto const& [locale, rev] : d.distribution.installed) {
    if (!first) result += ',';
    result += Quote(locale) + ":" + std::to_string(rev);
    first = false;
  }
  result += "},\"files\":[";
  for (std::size_t i = 0; i < state.files.size(); ++i) {
    if (i) result += ',';
    auto const& f = state.files[i];
    result += "{\"id\":" + Quote(f.id) + ",\"received\":" + std::to_string(f.received) + ",\"size\":" + std::to_string(f.size) + '}';
  }
  return result + "]}";
}
}
