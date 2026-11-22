#include "download.h"
#include "network.h"
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
TransferWorker::TransferWorker(fs::path profile, fs::path source, std::string provider, Report report, Ready ready)
    : profile_{std::move(profile)}, local_source_{std::move(source)}, provider_{std::move(provider)},
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
  auto error = [&](std::string message) { if (!canceled()) { state.phase = "error"; state.message = std::move(message); report(); } };
  std::unique_ptr<AetherPeer> peer;
  std::unique_ptr<Frame> incoming;
  std::uint64_t expected_request = 0;
  auto fetch = [&](Frame request_frame, Frame& response) -> bool {
    request_frame.request = static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
    expected_request = request_frame.request;
    incoming.reset();
    if (!local_source_.empty()) { response = Serve(local_source_, request_frame); return response.kind != 5; }
    if (!peer) {
      if (provider_.empty()) return false;
      peer = std::make_unique<AetherPeer>(profile_ / "local" / "download-identity", "update-lab-downloader");
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
          response = std::move(*frame); return response.kind != 5;
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
  Catalog catalog;
  Frame response;
  state.phase = "catalog"; state.message = "";
  if (!report()) return;
  // Cached catalog allows the candidate executable to boot without a second network round trip.
  Bytes catalog_bytes;
  if (!(ReadFile(profile_ / "local" / "catalog.bin", catalog_bytes) && ReadCatalog(catalog_bytes, catalog))) {
    if (!fetch({1, 1, "", 0, {}}, response) || !ReadCatalog(response.payload, catalog)) {
      error("Component catalog unavailable" + (peer ? peer->Diagnostics() : "")); return;
    }
    catalog_bytes = response.payload;
    if (!AtomicWrite(profile_ / "local" / "catalog.bin", catalog_bytes)) { error("Catalog save failed"); return; }
  }
  auto release = FindRelease(catalog, state.intent.release);
  if (!release) { error("Unknown release"); return; }
  if (std::find(release->languages.begin(), release->languages.end(), state.intent.language) == release->languages.end()) {
    error("Language is unavailable in this release"); return;
  }
  auto cache = profile_ / "local" / "components";
  std::error_code ec; fs::create_directories(cache, ec);
  if (ec) { error("Cache directory creation failed"); return; }
  std::vector<std::string> ids{release->supply, LanguageId(release->id, state.intent.language)};
  bool use_delta = false;
  std::string delta_id = "delta/" + std::to_string(request.current_release) + "/" + std::to_string(release->id);
  auto delta = FindComponent(catalog, delta_id);
  auto full = FindComponent(catalog, release->supply);
  auto base_path = cache / FileId("supply/" + std::to_string(request.current_release));
  if (!fs::exists(cache / FileId(release->supply)) && delta && full && fs::exists(base_path)) {
    ids[0] = delta_id; use_delta = true;
  }
  unsigned executable = state.intent.broken ? 2 : release->executable;
  if (executable != request.current_executable) ids.push_back(ExecutableId(executable));
  state.requested = ids;
  state.phase = "progress";
  std::uint64_t resumed = 0;
  for (auto const& id : ids) {
    auto c = FindComponent(catalog, id);
    if (!c) { error("Component missing for this platform: " + id); return; }
    auto it = std::find_if(state.files.begin(), state.files.end(), [&](auto const& f) { return f.id == id; });
    if (it == state.files.end()) { state.files.push_back({id, c->size, 0}); it = std::prev(state.files.end()); }
    if (fs::exists(cache / FileId(id))) it->received = c->size;
    resumed += it->received;
  }
  if (!report()) return;
  for (auto const& id : ids) {
    auto& file = *std::find_if(state.files.begin(), state.files.end(), [&](auto const& f) { return f.id == id; });
    auto final = cache / FileId(id);
    if (fs::exists(final)) continue;
    auto partial = final; partial += ".part";
    // File data is flushed BEFORE the progress Event. A crash may leave an uncommitted tail.
    if (fs::exists(partial)) fs::resize_file(partial, file.received, ec);
    else if (file.received) { error("Partial component file is missing"); return; }
    if (ec) { error("Cannot truncate uncommitted tail"); return; }
    while (file.received < file.size && !canceled()) {
      auto started = Clock::now();
      if (!fetch({1, 2, id, file.received / kChunkSize, {}}, response)) { error("Component download failed: " + id + (peer ? peer->Diagnostics() : "")); return; }
      if (!AppendDurable(partial, response.payload)) { error("Partial component write failed"); return; }
      file.received += response.payload.size();
      if (!report()) return;
      if (request.bytes_per_second) {
        auto until = started + std::chrono::microseconds{response.payload.size() * 1000000 / request.bytes_per_second};
        while (!canceled() && Clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds{10});
      }
    }
    if (canceled()) return;
    fs::rename(partial, final, ec);
    if (ec) { error("Component completion rename failed"); return; }
  }
  if (canceled()) return;
  TransferResult result{state.epoch, state.intent, catalog, {}, {}, {}, executable, resumed, use_delta};
  Bytes supply;
  if (!ReadFile(cache / FileId(ids[0]), supply) || !ReadFile(cache / FileId(ids[1]), result.language)) {
    error("Downloaded state cannot be read"); return;
  }
  if (use_delta) {
    Bytes base; ae::RamDomainStorage storage; unsigned version = request.current_release;
    if (!ReadFile(base_path, base) || !ApplyStatePackage(base, storage, version) ||
        !ApplyStatePackage(supply, storage, version)) { error("Delta source version does not match"); return; }
    result.supply = FullState(storage, release->id);
    if (!AtomicWrite(cache / FileId(release->supply), result.supply)) { error("Reconstructed state save failed"); return; }
  } else result.supply = std::move(supply);
  if (ids.size() == 3 && !ReadFile(cache / FileId(ids[2]), result.executable)) { error("Executable cannot be read"); return; }
  state.phase = "ready";
  state.message = "resumed=" + std::to_string(resumed) + (use_delta ? "; delta" : "; full");
  if (report()) ready_(std::move(result));
}
}
