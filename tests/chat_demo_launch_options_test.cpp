#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/runtime_node.h"
#include "chat_commands.h"
#include "chat_launch_options.h"
#include "chat_model.h"

namespace apptraverse::example::chat_demo {
namespace {

#define CHECK(cond)                                                           \
  do {                                                                        \
    if (!(cond)) {                                                            \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"        \
                << __LINE__ << '\n';                                          \
      std::exit(1);                                                           \
    }                                                                         \
  } while (0)

void TestPlainLaunch() {
  std::vector<std::string> args = {};
  auto res = ParseChatLaunchOptions(args);
  CHECK(res.ok);
  CHECK(!res.options.state_dir.has_value());
  CHECK(!res.options.open_peer.has_value());
}

void TestAdminIdOnly() {
  std::vector<std::string> args = {"--peer-admin-id", "123456789"};
  auto res = ParseChatLaunchOptions(args);
  CHECK(res.ok);
  CHECK(!res.options.state_dir.has_value());
  CHECK(res.options.open_peer.has_value());
  CHECK(res.options.open_peer->peer_admin_id == "123456789");
  CHECK(!res.options.open_peer->peer_aether_uid.has_value());
  CHECK(!res.options.open_peer->peer_name.has_value());
}

void TestAllSupportedOptions() {
  std::vector<std::string> args = {
      "--state-dir", "/path/to/state",
      "--peer-admin-id", "peer-999",
      "--peer-aether-uid", "aether-uid-xyz",
      "--peer-name", "Support Agent",
  };
  auto res = ParseChatLaunchOptions(args);
  CHECK(res.ok);
  CHECK(res.options.state_dir == "/path/to/state");
  CHECK(res.options.open_peer.has_value());
  CHECK(res.options.open_peer->peer_admin_id == "peer-999");
  CHECK(res.options.open_peer->peer_aether_uid == "aether-uid-xyz");
  CHECK(res.options.open_peer->peer_name == "Support Agent");
}

void TestSpacesAndNonAsciiInNameAndPath() {
  std::vector<std::string> args = {
      "--state-dir", "/home/user/App Data/Chat Привет",
      "--peer-admin-id", "admin-123",
      "--peer-name", "Иван Иванов (Support) 🚀",
  };
  auto res = ParseChatLaunchOptions(args);
  CHECK(res.ok);
  CHECK(res.options.state_dir == "/home/user/App Data/Chat Привет");
  CHECK(res.options.open_peer.has_value());
  CHECK(res.options.open_peer->peer_admin_id == "admin-123");
  CHECK(res.options.open_peer->peer_name == "Иван Иванов (Support) 🚀");
}

void TestMissingValue() {
  std::vector<std::string> args = {"--state-dir"};
  auto res = ParseChatLaunchOptions(args);
  CHECK(!res.ok);
  CHECK(!res.error_message.empty());

  std::vector<std::string> args2 = {"--peer-admin-id"};
  auto res2 = ParseChatLaunchOptions(args2);
  CHECK(!res2.ok);

  std::vector<std::string> args3 = {"--peer-admin-id", "123", "--peer-name"};
  auto res3 = ParseChatLaunchOptions(args3);
  CHECK(!res3.ok);
}

void TestOptionTokenAsMissingValue() {
  {
    std::vector<std::string> args = {"--state-dir", "--peer-admin-id", "abc"};
    auto res = ParseChatLaunchOptions(args);
    CHECK(!res.ok);
  }
  {
    std::vector<std::string> args = {"--peer-aether-uid", "--peer-name", "n"};
    auto res = ParseChatLaunchOptions(args);
    CHECK(!res.ok);
  }
}

void TestUnknownOrRepeatedOption() {
  // Unknown
  {
    std::vector<std::string> args = {"--foo", "bar"};
    auto res = ParseChatLaunchOptions(args);
    CHECK(!res.ok);
  }
  // Repeated --state-dir
  {
    std::vector<std::string> args = {"--state-dir", "dir1", "--state-dir", "dir2"};
    auto res = ParseChatLaunchOptions(args);
    CHECK(!res.ok);
  }
  // Repeated --peer-admin-id
  {
    std::vector<std::string> args = {"--peer-admin-id", "id1", "--peer-admin-id", "id2"};
    auto res = ParseChatLaunchOptions(args);
    CHECK(!res.ok);
  }
}

void TestUidWithoutAdminId() {
  // UID without admin id
  {
    std::vector<std::string> args = {"--peer-aether-uid", "uid-1"};
    auto res = ParseChatLaunchOptions(args);
    CHECK(!res.ok);
  }
  // Name without admin id
  {
    std::vector<std::string> args = {"--peer-name", "Name Only"};
    auto res = ParseChatLaunchOptions(args);
    CHECK(!res.ok);
  }
}

void TestRepeatedApplicationSelectsExistingChat() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto ws = ChatWorkspace::ptr::Create(ae::CreateWith{domain}.with_id(1));
  InitializeRuntimeNode(*ws);

  std::vector<std::string> args = {
      "--peer-admin-id", "admin-alpha",
      "--peer-name", "Alpha Peer",
  };
  auto res = ParseChatLaunchOptions(args);
  CHECK(res.ok);
  CHECK(res.options.open_peer.has_value());

  auto const& req = *res.options.open_peer;

  // First application creates chat
  auto e1 = OpenOrSelectChat(*ws, req.peer_admin_id,
                             req.peer_name.value_or(""));
  CHECK(e1.is_valid());
  CHECK(ws->chats.size() == 1);
  CHECK(ws->selected_chat_id == e1.id());
  CHECK(e1->display_name == "Alpha Peer");

  // Open another chat
  auto e2 = OpenOrSelectChat(*ws, "admin-beta", "Beta Peer");
  CHECK(ws->chats.size() == 2);
  CHECK(ws->selected_chat_id == e2.id());

  // Repeated application selects existing chat
  auto e1_again = OpenOrSelectChat(*ws, req.peer_admin_id,
                                   req.peer_name.value_or(""));
  CHECK(ws->chats.size() == 2);
  CHECK(e1_again.id() == e1.id());
  CHECK(ws->selected_chat_id == e1.id());
}

}  // namespace
}  // namespace apptraverse::example::chat_demo

int main() {
  using namespace apptraverse::example::chat_demo;
  std::cout << "Running apptraverse_chat_demo_launch_options_test...\n";

  TestPlainLaunch();
  TestAdminIdOnly();
  TestAllSupportedOptions();
  TestSpacesAndNonAsciiInNameAndPath();
  TestMissingValue();
  TestOptionTokenAsMissingValue();
  TestUnknownOrRepeatedOption();
  TestUidWithoutAdminId();
  TestRepeatedApplicationSelectsExistingChat();

  std::cout << "All launch options tests passed!\n";
  return 0;
}
