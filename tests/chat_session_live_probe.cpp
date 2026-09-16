// Session-level live probe: one ChatSession with the default Aether endpoint.
// Real-network validation is run separately (Linux apptraverse_chat_aether_p2p_test).
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

#include "apptraverse/object_macros.h"

#include "chat_session.h"

int main(int argc, char* argv[]) {
  std::filesystem::path state_dir;
  std::string peer_uid;
  std::uint64_t run_ms = 5000;

  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    } else if (arg == "--peer-uid" && i + 1 < argc) {
      peer_uid = argv[++i];
    } else if (arg == "--run-ms" && i + 1 < argc) {
      run_ms = static_cast<std::uint64_t>(std::stoull(argv[++i]));
    }
  }

  if (state_dir.empty()) {
    std::cerr << "Usage: apptraverse_chat_session_live_probe --state-dir <dir> "
                 "[--peer-uid <uid>] [--run-ms <n>]\n";
    return 1;
  }

  apptraverse::EnsureObjectRegistration();
  apptraverse::example::chat_demo::EnsureChatDemoModelRegistration();

  using apptraverse::example::chat_demo::ChatSession;
  using apptraverse::example::chat_demo::ChatSessionConfig;
  using apptraverse::example::chat_demo::OpenPeerRequest;
  using apptraverse::example::chat_demo::SessionLifecycleState;

  ChatSession session;
  if (!session.Start(ChatSessionConfig{.state_dir = state_dir}, [] {})) {
    std::cerr << "ChatSession Start failed\n";
    return 1;
  }

  auto const deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(run_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    auto status = session.GetRuntimeStatus();
    if (status.lifecycle_state == SessionLifecycleState::kReady) {
      break;
    }
    if (status.lifecycle_state == SessionLifecycleState::kFailed) {
      std::cerr << "ChatSession failed: " << status.error_text << '\n';
      session.RequestStop();
      session.Join();
      return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  if (!peer_uid.empty()) {
    session.OpenPeer(OpenPeerRequest{
        .peer_admin_id = "probe-peer",
        .peer_aether_uid = peer_uid,
    });
  }

  while (std::chrono::steady_clock::now() < deadline) {
    (void)session.TryTakeUiUpdate();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  session.RequestStop();
  session.Join();
  return 0;
}
