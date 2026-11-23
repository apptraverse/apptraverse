#include "session.h"
#include <chrono>
#include <iostream>
#include "apptraverse/noninteractive_crt.h"
using namespace update_lab;
int update_lab::RunHeadless(Options const& options) {
  apptraverse::ApplicationModelRuntime runtime;
  apptraverse::ModelObjectProxy proxy{[&](auto work) { runtime.Post(std::move(work)); }};
  std::mutex mutex; std::condition_variable condition;
  bool published = false, ended = false;
  int result = 1;
  std::thread model{[&] {
    Session session{options, runtime};
    result = session.Run([&](auto) { std::lock_guard lock{mutex}; published = true; condition.notify_all(); });
    std::lock_guard lock{mutex}; ended = true; condition.notify_all();
  }};
  std::unique_ptr<apptraverse::ApplicationUiSession> ui;
  bool initial = false, stop_requested = false;
  auto started = std::chrono::steady_clock::now();
  for (;;) {
    { std::unique_lock lock{mutex}; condition.wait_for(lock, std::chrono::milliseconds{20}, [&] { return published || ended; });
      if (ended) break; published = false; }
    auto bytes = runtime.TakePublishedCopyForUiApply();
    if (!bytes.empty()) {
      if (!ui) ui = std::make_unique<apptraverse::ApplicationUiSession>(bytes, nullptr, proxy, nullptr, false);
      else ui->ApplyStructural(bytes);
      runtime.NotifyPublicationConsumed();
      auto& view = static_cast<LabView&>(ui->root());
      auto json = InspectJson(view);
      std::cout << json << std::endl;
      if (!options.snapshot_file.empty() && !AtomicWrite(options.snapshot_file, Bytes{json.begin(), json.end()})) Fail("snapshot write failed");
      if (!initial) { initial = true; proxy.Invoke(kViewId, &LabView::Submit, Command{"initial", "", 0, 0, 0, 0}); }
      AcknowledgeHealth(options, view);
      bool complete = view.data.commands_done &&
          (view.download->state.phase == "activated" || view.download->state.phase == "idle" ||
           view.download->state.phase == "error" || view.download->state.phase == "rolled-back");
      if (!stop_requested && (view.data.restarting || (complete && options.hold_ms == 0 && !options.suppress_health))) {
        runtime.RequestStop(); stop_requested = true;
      }
    }
    if (!stop_requested && options.hold_ms &&
        std::chrono::steady_clock::now() - started >= std::chrono::milliseconds{options.hold_ms}) {
      runtime.RequestStop(); stop_requested = true;
    }
  }
  runtime.AbandonUnconsumedPublicationsForTeardown(); model.join(); return result;
}
int main(int argc, char** argv) {
  apptraverse::EnableNoninteractiveCrt();
#if UPDATE_LAB_EXECUTABLE == 2
  (void)argc; (void)argv;
  std::cerr << "INTENTIONAL_BAD_EXECUTABLE\n";
  return 86;
#else
  RegisterModel();
  Options options;
  for (int n = 1; n < argc; ++n) {
    std::string a = argv[n];
    auto next = [&]() -> std::string { if (n + 1 == argc) Fail("missing option value"); return argv[++n]; };
    if (a == "--profile") options.profile = fs::absolute(next());
    else if (a == "--install") options.install = fs::absolute(next());
    else if (a == "--store") options.store = fs::absolute(next());
    else if (a == "--provider") options.provider = next();
    else if (a == "--health-file") options.health_file = fs::absolute(next());
    else if (a == "--health-token") options.health_token = next();
    else if (a == "--snapshot-file") options.snapshot_file = fs::absolute(next());
    else if (a == "--export") options.export_file = fs::absolute(next());
    else if (a == "--import") options.import_file = fs::absolute(next());
    else if (a == "--language") options.language = next();
    else if (a == "--note") options.note = next();
    else if (a == "--device") options.device = next();
    else if (a == "--rename-first") options.rename = next();
    else if (a == "--badge-first") options.badge = next();
    else if (a == "--latest") options.latest = true;
    else if (a == "--release") options.release = std::stoi(next());
    else if (a == "--rate") options.rate = std::stoi(next());
    else if (a == "--add-clone") ++options.add_clones;
    else if (a == "--headless") options.headless = true;
    else if (a == "--native-smoke") options.native_smoke = true;
    else if (a == "--broken") options.broken = true;
    else if (a == "--no-action") options.no_action = true;
    else if (a == "--suppress-health") options.suppress_health = true;
    else if (a == "--hold-ms") options.hold_ms = static_cast<unsigned>(std::stoul(next()));
    else if (a == "--auto-close-ms") options.auto_close_ms = static_cast<unsigned>(std::stoul(next()));
    else Fail("unknown application option: " + a);
  }
  if (options.profile.empty()) options.profile = fs::absolute("update-lab-profile");
  FileLock lock{options.profile / "application.lock"};
  if (!lock.acquired()) Fail("profile is already open");
  return options.headless ? RunHeadless(options) : RunNative(options);
#endif
}
