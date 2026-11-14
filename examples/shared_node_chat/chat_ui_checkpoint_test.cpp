#include "chat_ui_session.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>

#include "apptraverse/distill.h"
#include "apptraverse/distilled_domain_export.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/single_file_user_storage.h"

using namespace apptraverse;
using namespace apptraverse::chat::gui;
using namespace std::chrono_literals;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "FAILED " #condition << '\n'; std::abort(); } } while (false)

struct Host : Presentation {
  void PresentWindow(ChatUi const&) override {}
  void RemoveWindow() override {}
  void Present(Control, ControlState const&, ChatUi const&) override {}
  void Remove(Control) override {}
};

struct FlushGate {
  std::mutex mu;
  std::condition_variable cv;
  unsigned blocks{}, entered{}, released{};
  bool Flush() {
    std::unique_lock lock{mu};
    if (blocks == 0) return true;
    unsigned const slot = ++entered;
    cv.notify_all();
    if (slot <= blocks) cv.wait(lock, [&] { return released >= slot; });
    return true;
  }
  void Arm() { std::lock_guard lock{mu}; blocks = 2; }
  void Wait(unsigned slot) {
    std::unique_lock lock{mu};
    CHECK(cv.wait_for(lock, 10s, [&] { return entered >= slot; }));
  }
  void Release(unsigned slot) {
    std::lock_guard lock{mu}; released = slot; cv.notify_all();
  }
};

template<class Predicate>
void Until(Session& session, Host& host, Predicate ready) {
  auto const limit = std::chrono::steady_clock::now() + 40s;
  while (!ready()) {
    CHECK(std::chrono::steady_clock::now() < limit);
    session.ApplyPublication(host);
    std::this_thread::sleep_for(2ms);
  }
}

int main(int argc, char** argv) {
  CHECK(argc == 2);
  EnableNoninteractiveCrt();
  EnsureUiRegistration();
  auto const profile = std::filesystem::absolute(argv[1]);
  CHECK(!std::filesystem::exists(profile));
  FlushGate flush;
  Session session{profile, "Checkpoint test", true, "", [] {}, true,
                  [&] { return flush.Flush(); }};
  bool rejected = false;
  session.Checkpoint([&](bool saved) { CHECK(!saved); rejected = true; });
  CHECK(rejected);
  Host host;
  session.Start();
  Until(session, host, [&] {
    auto* model = session.Mirror();
    return model && model->connection.ready && model->connection.in_room;
  });

  // Hold the model queue while the caller submits input + Send + deactivation.
  // A checkpoint must join that queue before it enters the transport queue.
  std::promise<void> entered, release;
  auto gate = release.get_future();
  auto reached = entered.get_future();
  session.Proxy().EnqueueModelWork([&](ae::Domain&) {
    entered.set_value();
    gate.wait();
  });
  CHECK(reached.wait_for(5s) == std::future_status::ready);
  session.Proxy().Invoke(RootId(), &ChatUi::EditDraft, std::string{"accepted-before-checkpoint"});
  session.Proxy().Invoke(RootId(), &ChatUi::Send);
  std::promise<bool> completed;
  auto result = completed.get_future();
  std::atomic<unsigned> callbacks{0};
  session.Checkpoint([&](bool saved) {
    CHECK(callbacks.fetch_add(1) == 0);
    completed.set_value(saved);
  });
  release.set_value();
  Until(session, host, [&] { return result.wait_for(0ms) == std::future_status::ready; });
  CHECK(result.get());

  // Inspect the checkpoint before Stop can overwrite it with a newer snapshot.
  ae::RamDomainStorage user, backing;
  CHECK(LoadUserStorageFile(user, profile));
  {
    ae::Domain producer{backing};
    auto root = BuildUi(producer);
    FinalizeDistilledGraph(*root);
    StabilizeDistilledPersistentSnapshot(*root);
    SaveDistilledRoot(*root);
  }
  std::set<ae::ObjId> constants;
  for (auto id : CollectStoredObjectIds(backing))
    if (backing.IsConstantObject(id)) constants.insert(id);
  OverlayDomainStorage storage{user, backing, constants};
  ae::Domain cold{storage};
  auto restored = LoadApplication<ChatUi>(cold, RootId());
  CHECK(restored->editing.draft.empty());
  CHECK(!restored->editing.sending);
  CHECK(restored->editing.configured && restored->editing.host);
  CHECK(restored->editing.name == "Checkpoint test");
  CHECK(std::filesystem::exists(profile / "transport" / "user.bin"));

  // A second checkpoint has flushed transport, but a later Send is accepted
  // before its final UI save. Block actual platform durability for that Send,
  // making the previous persisted-sending race deterministic without sleeps.
  flush.Arm();
  std::promise<bool> later_completed;
  auto later_result = later_completed.get_future();
  std::atomic<unsigned> later_callbacks{0};
  session.Checkpoint([&](bool saved) {
    CHECK(later_callbacks.fetch_add(1) == 0);
    later_completed.set_value(saved);
  });
  flush.Wait(1); // The checkpoint's transport flush is held.
  std::promise<void> late_send_submitted;
  auto late_send_ready = late_send_submitted.get_future();
  session.Proxy().EnqueueModelWork([&](ae::Domain& model) {
    auto* root = model.Find(RootId()).as<ChatUi>();
    root->EditDraft("accepted-after-checkpoint-barrier");
    root->Send();
    CHECK(root->editing.sending);
    late_send_submitted.set_value();
  });
  CHECK(late_send_ready.wait_for(5s) == std::future_status::ready);
  flush.Release(1);
  flush.Wait(2); // Checkpoint queued its UI save before transport began Send.
  std::promise<void> final_save_attempted;
  auto attempted = final_save_attempted.get_future();
  session.Proxy().EnqueueModelWork([&](ae::Domain& model) {
    auto* root = model.Find(RootId()).as<ChatUi>();
    CHECK(root->editing.sending);
    // Ordinary input still runs while only the checkpoint completion waits.
    root->EditDraft("typed-while-checkpoint-waits");
    final_save_attempted.set_value();
  });
  CHECK(attempted.wait_for(5s) == std::future_status::ready);
  CHECK(later_result.wait_for(0ms) != std::future_status::ready);
  flush.Release(2);
  Until(session, host, [&] { return later_result.wait_for(0ms) == std::future_status::ready; });
  CHECK(later_result.get());

  ae::RamDomainStorage final_user, transport_checkpoint;
  CHECK(LoadUserStorageFile(final_user, profile));
  CHECK(LoadUserStorageFile(transport_checkpoint, profile / "transport"));
  OverlayDomainStorage final_storage{final_user, backing, constants};
  ae::Domain final_cold{final_storage};
  auto final_root = LoadApplication<ChatUi>(final_cold, RootId());
  CHECK(!final_root->editing.sending);
  CHECK(final_root->editing.draft == "typed-while-checkpoint-waits");
  session.Stop();
  CHECK(callbacks == 1);
  CHECK(later_callbacks == 1);
  rejected = false;
  session.Checkpoint([&](bool saved) { CHECK(!saved); rejected = true; });
  CHECK(rejected);

  // Load the exact transport USER captured above through its real owner after
  // stopping the original SDK identity. Stop cannot mask an incomplete save.
  auto const transport_profile = profile / "checkpoint-transport-consumer";
  CHECK(!std::filesystem::exists(transport_profile));
  std::filesystem::create_directory(transport_profile);
  std::filesystem::copy(profile / "transport" / "sdk", transport_profile / "sdk",
                        std::filesystem::copy_options::recursive);
  CHECK(SaveUserStorageFile(transport_checkpoint, transport_profile));
  apptraverse::chat::ChatSession transport{transport_profile, "Checkpoint test"};
  transport.Start();
  auto const cold_limit = std::chrono::steady_clock::now() + 40s;
  for (;;) {
    auto value = transport.View();
    if (value.ready && value.in_room) {
      bool found = false;
      for (auto const& message : value.message_records)
        found = found || message.text == "accepted-after-checkpoint-barrier";
      CHECK(found);
      break;
    }
    CHECK(std::chrono::steady_clock::now() < cold_limit);
    std::this_thread::sleep_for(2ms);
  }
  transport.Stop();
  std::cout << "PASS: prior/later Sends settle before checkpoint; cold UI+transport USER agree; input runs; callbacks once\n";
}
