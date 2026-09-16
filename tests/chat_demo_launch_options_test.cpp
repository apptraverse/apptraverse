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

void TestHostParse() {
  auto res = ParseChatLaunchOptions(std::vector<std::string>{"--host"});
  CHECK(res.ok);
  CHECK(res.options.role == DemoRole::kHost);
  CHECK(!res.options.host_uid_prefill.has_value());
}

void TestClientParse() {
  auto res = ParseChatLaunchOptions(std::vector<std::string>{"--client"});
  CHECK(res.ok);
  CHECK(res.options.role == DemoRole::kClient);
}

void TestClientPrefill() {
  auto res = ParseChatLaunchOptions(
      std::vector<std::string>{"--client", "--host-uid",
                               "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"});
  CHECK(res.ok);
  CHECK(res.options.host_uid_prefill == "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
}

void TestNoRoleAndBothRoles() {
  auto none = ParseChatLaunchOptions(std::vector<std::string>{});
  CHECK(!none.ok);
  auto both =
      ParseChatLaunchOptions(std::vector<std::string>{"--host", "--client"});
  CHECK(!both.ok);
}

void TestMissingValuesAndUnicodePath() {
  auto missing = ParseChatLaunchOptions(std::vector<std::string>{"--host", "--state-dir"});
  CHECK(!missing.ok);
  auto unicode = ParseChatLaunchOptions(std::vector<std::string>{
      "--client", "--state-dir", "/home/user/App Data/Chat Привет"});
  CHECK(unicode.ok);
  CHECK(unicode.options.state_dir == "/home/user/App Data/Chat Привет");
}

void TestRetiredOptionsRejected() {
  for (auto const* flag : {"--peer-admin-id", "--peer-aether-uid", "--peer-name"}) {
    auto res = ParseChatLaunchOptions(std::vector<std::string>{"--host", flag, "x"});
    CHECK(!res.ok);
  }
  auto host_uid_on_host =
      ParseChatLaunchOptions(std::vector<std::string>{"--host", "--host-uid", "x"});
  CHECK(!host_uid_on_host.ok);
}

void TestHelpAndRepeated() {
  auto help = ParseChatLaunchOptions(std::vector<std::string>{"--help"});
  CHECK(help.ok);
  CHECK(help.options.show_help);
  auto repeated = ParseChatLaunchOptions(
      std::vector<std::string>{"--host", "--state-dir", "a", "--state-dir", "b"});
  CHECK(!repeated.ok);
}

void TestUidEntryRoundTrip() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto ws = ChatWorkspace::ptr::Create(ae::CreateWith{domain}.with_id(1));
  InitializeRuntimeNode(*ws);
  CHECK(ConfigureDemoRole(*ws, DemoRole::kClient));
  std::string const uid = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";
  auto e1 = OpenOrSelectChat(*ws, uid);
  CHECK(e1.is_valid());
  CHECK(ws->chats.size() == 1);
  auto e1_again = OpenOrSelectChat(*ws, uid);
  CHECK(e1_again.id() == e1.id());
  CHECK(ws->chats.size() == 1);
}

}  // namespace
}  // namespace apptraverse::example::chat_demo

int main() {
  using namespace apptraverse::example::chat_demo;
  TestHostParse();
  TestClientParse();
  TestClientPrefill();
  TestNoRoleAndBothRoles();
  TestMissingValuesAndUnicodePath();
  TestRetiredOptionsRejected();
  TestHelpAndRepeated();
  TestUidEntryRoundTrip();
  std::cout << "All launch options tests passed!\n";
  return 0;
}
