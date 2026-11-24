#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <thread>
#include "apptraverse/distilled_domain_export.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/single_file_user_storage.h"
#include "chat_ui_session.h"
using namespace apptraverse;
using namespace apptraverse::chat::gui;
using namespace std::chrono_literals;
#define CHECK(condition)                         \
  do {                                           \
    if (!(condition)) {                          \
      std::cerr << "FAILED " #condition << '\n'; \
      std::abort();                              \
    }                                            \
  } while (false)
template <class P>
void Until(Session& session, P ready) {
  auto limit = std::chrono::steady_clock::now() + 30s;
  while (!ready()) {
    CHECK(std::chrono::steady_clock::now() < limit);
    session.ApplyPublication(static_cast<void*>(nullptr));
    std::this_thread::sleep_for(2ms);
  }
}
int main(int argc, char** argv) {
  CHECK(argc == 2);
  EnableNoninteractiveCrt();
  EnsureUiRegistration();
  ConfigureUiFactories(HeadlessUiFactories());
  auto profile = std::filesystem::absolute(argv[1]);
  CHECK(!std::filesystem::exists(profile));
  auto secret = std::string{"ram-only-draft-unique-marker-2026"};
  ae::ObjId device_id;
  {
    Session session{profile, "Ephemeral Name", false, "", [] {}, true};
    bool rejected = false;
    session.Checkpoint([&](bool saved) {
      CHECK(!saved);
      rejected = true;
    });
    CHECK(rejected);
    session.Start();
    Until(session, [&] { return session.Mirror() != nullptr; });
    device_id = session.Mirror()->device_state.id();
    session.Proxy().Invoke(RootId(), &ChatUi::EditDraft, secret);
    session.Proxy().Invoke(
        device_id, &ui::UiDeviceState::SetAppearance,
        ui::UiAppearancePreferences{ui::UiThemeMode::kDark, true});
    std::promise<bool> done;
    auto future = done.get_future();
    session.Checkpoint([&](bool saved) { done.set_value(saved); });
    Until(session,
          [&] { return future.wait_for(0ms) == std::future_status::ready; });
    CHECK(future.get());
    std::vector<std::uint8_t> user_bytes, local_bytes;
    CHECK(session.CaptureUser(user_bytes) && session.CaptureLocal(local_bytes));
    ae::RamDomainStorage user, local;
    CHECK(DeserializeUserStorage(user, user_bytes) &&
          DeserializeUserStorage(local, local_bytes));
    CHECK(CollectStoredObjectIds(user).empty());
    auto local_ids = CollectStoredObjectIds(local);
    CHECK(!local_ids.empty() && !local_ids.contains(RootId()));
    CHECK(!local_ids.contains(session.Mirror()->participants.id()));
    for (auto const& text : session.Mirror()->text_controls)
      CHECK(!local_ids.contains(text.id()));
    for (auto const& button : session.Mirror()->action_buttons)
      CHECK(!local_ids.contains(button.id()));
    CHECK(std::search(local_bytes.begin(), local_bytes.end(), secret.begin(),
                      secret.end()) == local_bytes.end());
    CHECK(!std::filesystem::exists(profile / "user.bin"));
    CHECK(!std::filesystem::exists(profile / "transport" / "user.bin"));
    CHECK(std::filesystem::exists(profile / "local" / "user.bin"));
    // The native owner may close before applying the next snapshot. The
    // command already accepted by the model still belongs in the final save.
    session.Proxy().Invoke(
        device_id, &ui::UiDeviceState::SetAppearance,
        ui::UiAppearancePreferences{ui::UiThemeMode::kDark, false});
    session.Stop();
  }
  {
    Session session{profile, "Fresh Name", false, "", [] {}, true};
    session.Start();
    Until(session, [&] { return session.Mirror() != nullptr; });
    auto const* root = session.Mirror();
    CHECK(root->editing.name == "Fresh Name" && root->editing.draft.empty());
    CHECK(!root->editing.sending && root->pending_sends.empty() &&
          root->connection.messages.empty());
    CHECK(root->device_state.id() == device_id);
    CHECK(root->device_state->appearance.theme == ui::UiThemeMode::kDark);
    CHECK(!root->device_state->appearance.soften_dark);
    session.Stop();
  }
  CHECK(!std::filesystem::exists(profile / "user.bin"));
  CHECK(!std::filesystem::exists(profile / "transport" / "user.bin"));
  std::cout << "PASS: RAM-only chat, LOCAL checkpoint/restore, no business "
               "profile IO\n";
}
