#include "download.h"
#include "network.h"
#include "install.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
namespace update_lab {
using Clock = std::chrono::steady_clock;
bool AppendDurable(fs::path const& path, Bytes const& bytes) {
#ifdef _WIN32
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
      written == bytes.size() && FlushFileBuffers(file);
  if (!CloseHandle(file)) ok = false;
  return ok;
#else
  int file = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
  if (file < 0) return false;
  std::size_t offset = 0;
  bool ok = true;
  while (offset < bytes.size()) {
    auto n = write(file, bytes.data() + offset, bytes.size() - offset);
    if (n <= 0) { ok = false; break; }
    offset += static_cast<std::size_t>(n);
  }
  if (fsync(file)) ok = false;
  if (close(file)) ok = false;
  return ok;
#endif
}
TransferWorker::TransferWorker(fs::path profile, fs::path source, std::string provider, std::string app,
                               Report report, Ready ready, bool preview_worker)
    : profile_{std::move(profile)}, local_source_{std::move(source)}, provider_{std::move(provider)}, app_{std::move(app)},
      identity_{profile_ / "local" / (preview_worker ? "catalog-identity" : "download-identity")},
      report_{std::move(report)}, ready_{std::move(ready)}, thread_{[this] { Run(); }} {}
TransferWorker::~TransferWorker() { Stop(); }
void TransferWorker::Stop() {
  stop_ = true; condition_.notify_all();
  if (thread_.joinable()) thread_.join();
}
void TransferWorker::Begin(TransferRequest request) {
  wanted_ = request.snapshot.epoch;
  { std::lock_guard lock{mutex_}; pending_ = std::make_unique<TransferRequest>(std::move(request)); }
  condition_.notify_one();
}
void TransferWorker::Cancel(std::uint64_t next_epoch) {
  std::lock_guard lock{mutex_};
  wanted_ = next_epoch;
  pending_.reset();
}
void TransferWorker::Run() {
  while (!stop_) {
    std::unique_ptr<TransferRequest> next;
    { std::unique_lock lock{mutex_}; condition_.wait(lock, [&] { return stop_ || pending_; });
      if (stop_) return; next = std::move(pending_); }
    Transfer(std::move(*next));
  }
}
void TransferWorker::Transfer(TransferRequest request) {
  auto state = request.snapshot;
  auto canceled = [&] { return stop_.load() || wanted_.load() != state.epoch; };
  auto report = [&] { return !canceled() && report_(state); };
  auto error = [&](std::string message) {
    if (canceled()) return;
    if (request.preview) {
      ready_({state.epoch, state.intent, {}, {}, 0, true, std::move(message)});
    } else {
      state.phase = "error"; state.message = std::move(message); report();
    }
  };
  std::unique_ptr<AetherPeer> peer;
  std::unique_ptr<Frame> incoming;
  std::uint64_t expected_request = 0;
  auto fetch = [&](Frame request_frame, Frame& response) -> bool {
    if (!app_.empty()) request_frame.id = "apps/" + app_ + "/" + request_frame.id;
    response = {};
    request_frame.request = static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
    expected_request = request_frame.request;
    incoming.reset();
    if (!local_source_.empty()) { response = Serve(local_source_, request_frame); return response.protocol == kDeliveryProtocol && response.kind != ServiceError; }
    if (!peer) {
      if (provider_.empty()) return false;
      peer = std::make_unique<AetherPeer>(identity_, request.preview ? "update-lab-catalog" : "update-lab-downloader");
      peer->Receive([&](std::string const& from, Frame frame) {
        if (from == provider_ && frame.request == expected_request) incoming = std::make_unique<Frame>(std::move(frame));
      });
    }
    auto deadline = Clock::now() + std::chrono::seconds{60};
    auto resend = Clock::time_point{};
    while (!canceled() && Clock::now() < deadline) {
      peer->Tick();
      if (peer->failed()) return false;
      if (incoming) {
        auto frame = std::move(incoming);
        if (frame->request == request_frame.request) {
          response = std::move(*frame);
          return response.protocol == kDeliveryProtocol && response.id == request_frame.id &&
              response.chunk == request_frame.chunk && response.kind != ServiceError;
        }
      }
      if (!peer->Endpoint().empty() && Clock::now() >= resend) {
        peer->Send(provider_, request_frame);
        resend = Clock::now() + std::chrono::seconds{2};
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    return false;
  };
  Frame response{};
  auto cache = Cache(profile_);
  std::error_code ec; fs::create_directories(cache, ec);
  if (ec) { error("Cache directory creation failed"); return; }

  UpdatePlan plan{};
  auto prepared_path = cache / "prepared-plan.bin";
  Bytes prepared;
  if (request.prepared && fs::is_regular_file(prepared_path)) {
    if (!ReadFile(prepared_path, prepared) || !Decode(prepared, plan)) { error("Prepared update unavailable"); return; }
  } else {
    ResolveRequest query{Platform(), request.installed.release.id, request.current_executable,
                         state.intent, request.installed.resources, {}};
    // A profile can retain its native inventory across a codec change. Credit
    // only snapshots and partial files readable by this executable.
    if (!HasCachedComponent(cache, request.installed.release.supply)) query.current = kLatest;
    for (auto& [locale, revision] : query.installed) {
      if (!HasCachedComponent(cache, LanguageId(revision, locale))) revision = kLatest;
    }
    for (auto& f : state.files) {
      auto path = cache / FileId(f.id);
      auto partial = path; partial += ".part";
      if (HasCachedComponent(cache, f.id)) f.received = f.size;
      else if (!fs::exists(partial)) f.received = 0;
      query.received[f.id] = f.received;
    }
    // Reconstructed full resources can also satisfy a later first-install plan.
    fs::recursive_directory_iterator entry{cache, ec}, end;
    for (; !ec && entry != end; entry.increment(ec)) {
      auto extension = entry->path().extension();
      if (extension != ".component" && extension != ".native") continue;
      auto id = entry->path().lexically_relative(cache).replace_extension().generic_string();
      query.received[id] = extension == ".native" ? UINT64_MAX : entry->file_size(ec);
    }
    if (ec) { error("Cannot read component cache"); return; }
    if (!request.preview) { state.phase = "catalog"; state.message.clear(); if (!report()) return; }
    bool preview_offer = request.preview && !app_.empty();
    auto kind = preview_offer ? PreviewUpdate : ResolveUpdate;
    UpdateOffer offer;
    bool ok = fetch({kDeliveryProtocol, kind, "resolve-update", 0, Encode(query)}, response) &&
        (preview_offer ? response.kind == UpdateOffered && Decode(response.payload, offer)
                       : response.kind == UpdateResolved && Decode(response.payload, plan));
    if (ok && preview_offer) plan = std::move(offer.preferred);
    std::string message;
    if (!ok) message = response.kind == ServiceError && !response.payload.empty()
        ? std::string(response.payload.begin(), response.payload.end())
        : "Update service unavailable" + (peer ? peer->Diagnostics() : "");
    if (request.preview) {
      if (!canceled()) ready_({state.epoch, state.intent, std::move(plan), {}, 0, true, message, offer.data_only_remaining});
      return;
    }
    if (!ok) { error(message); return; }
    state.intent = plan.intent; // Pin latest to a concrete release before the first chunk.
  }

  state.requested.clear();
  state.phase = "progress";
  std::uint64_t resumed = 0;
  for (auto const& c : plan.files) {
    state.requested.push_back(c.id);
    auto it = std::find_if(state.files.begin(), state.files.end(), [&](auto const& f) { return f.id == c.id; });
    if (it == state.files.end()) { state.files.push_back({c.id, c.size, 0}); it = std::prev(state.files.end()); }
    if (it->size != c.size) { it->size = c.size; it->received = 0; }
    if (HasCachedComponent(cache, c.id)) it->received = c.size;
    resumed += it->received;
  }
  if (!report()) return;
  for (auto const& id : state.requested) {
    auto& file = *std::find_if(state.files.begin(), state.files.end(), [&](auto const& f) { return f.id == id; });
    auto final = cache / FileId(id);
    if (HasCachedComponent(cache, id)) continue;
    fs::create_directories(final.parent_path(), ec);
    if (ec) { error("Cannot create component directory"); return; }
    auto partial = final; partial += ".part";
    // File data is flushed BEFORE the progress Event. A crash may leave an uncommitted tail.
    if (fs::exists(partial)) fs::resize_file(partial, file.received, ec);
    else if (file.received) { error("Partial component file is missing"); return; }
    if (ec) { error("Cannot truncate uncommitted tail"); return; }
    bool simulate_delay = !app_.empty() && id.starts_with("language/");
    auto rate = app_.empty() || simulate_delay ? request.bytes_per_second : 0;
    auto wait_for_rate = [&](Clock::time_point start, std::size_t size) {
      auto until = start + std::chrono::microseconds{size * 1000000 / rate};
      while (!canceled() && Clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds{10});
    };
    while (file.received < file.size && !canceled()) {
      auto started = Clock::now();
      if (!fetch({kDeliveryProtocol, RequestChunk, id, file.received / kChunkSize, {}}, response)) { error("Component download failed: " + id + (peer ? peer->Diagnostics() : "")); return; }
      if (response.kind != Chunk || response.payload.size() != std::min(kChunkSize, file.size - file.received)) {
        error("Invalid component block: " + id);
        return;
      }
      if (simulate_delay && rate) wait_for_rate(Clock::now(), response.payload.size());
      if (canceled()) return;
      if (!AppendDurable(partial, response.payload)) { error("Partial component write failed"); return; }
      file.received += response.payload.size();
      if (!report()) return;
      if (!simulate_delay && rate) wait_for_rate(started, response.payload.size());
    }
    if (canceled()) return;
    fs::rename(partial, final, ec);
    if (ec) { error("Component completion rename failed"); return; }
  }
  if (canceled()) return;
  auto materialize = [&](Chain const& chain) {
    if (chain.steps.empty() || chain.steps.front() == chain.target)
      return HasCachedComponent(cache, chain.target);
    ae::RamDomainStorage storage;
    unsigned version = 0;
    if (!LoadCachedState(cache, chain.base, storage, version)) return false;
    for (auto const& step : chain.steps) {
      Bytes delta;
      if (!ReadFile(cache / FileId(step), delta) || !ApplyStatePackage(delta, storage, version)) return false;
    }
    return version == chain.revision && SaveCachedState(cache, chain.target, storage, version);
  };
  if (!materialize(plan.supply)) { error("Cannot apply base image chain"); return; }
  for (auto const& chain : plan.resources)
    if (!materialize(chain)) { error("Cannot apply resource chain: " + chain.target); return; }
  Bytes exe;
  if (plan.executable != request.current_executable && !ReadFile(cache / FileId(plan.executable_id), exe)) {
    error("Executable cannot be read"); return;
  }
  if (!AtomicWrite(prepared_path, Encode(plan))) { error("Cannot save prepared plan"); return; }
  TransferResult result{state.epoch, plan.intent, std::move(plan), std::move(exe), resumed, false, {}};
  state.phase = "ready";
  if (report()) ready_(std::move(result));
}
}
