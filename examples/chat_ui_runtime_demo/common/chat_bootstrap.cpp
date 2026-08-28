#include "chat_bootstrap.h"

#include <cassert>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

#include "chat_commands.h"
#include "chat_ids.h"
#include "chat_log.h"

namespace apptraverse {

Application::ptr BuildChatGraph(ae::Domain& domain, std::string host_name) {
  if (host_name.empty()) {
    host_name = chat::kDefaultHostName;
  }
  using chat::ChatObjId;
  using chat::ToObjId;

  auto application = Application::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ChatObjId::Application)));
  auto chat_room = ChatRoom::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ChatObjId::ChatRoom)));
  auto host_client = ChatClient::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ChatObjId::HostClient)));
  auto host_name_obj = ImmutableString::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ChatObjId::HostDisplayName)));
  host_name_obj->bytes = std::move(host_name);
  host_client->display_name = host_name_obj;

  auto local_aether = LocalAetherIdentity::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ChatObjId::LocalAetherIdentity)));
  auto uid_placeholder = ImmutableString::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ChatObjId::LocalAetherUidText)));
  uid_placeholder->bytes = "...";
  local_aether->uid_text = uid_placeholder;

  application->chat_room = chat_room;
  application->host_client = host_client;
  application->local_aether = local_aether;
  return application;
}

void CommitHostJoin(Application& application) {
  assert(application.chat_room.is_valid());
  assert(application.host_client.is_valid());
  CommitJoinChat(*application.chat_room, *application.host_client);
}

void DistillChatModel(std::filesystem::path const& dir, std::string host_name) {
  std::filesystem::remove_all(dir);
  DirectoryDomainStorage storage{dir};
  ae::Domain domain{ae::Now(), storage};
  auto application = BuildChatGraph(domain, std::move(host_name));
  FinalizeDistilledGraph(*application);
  CommitHostJoin(*application);
  SaveDistilledRoot(*application);  // runtime-save-ok: distill
  chat::ChatLog("distilled chat_ui_runtime_demo model to " + dir.string());
}

ChatRuntime LoadChatModel(std::filesystem::path const& dir) {
  if (!std::filesystem::exists(dir)) {
    throw std::runtime_error("distilled state missing: " + dir.string());
  }
  using chat::ChatObjId;
  using chat::ToObjId;

  ChatRuntime runtime;
  runtime.storage = std::make_unique<DirectoryDomainStorage>(dir);
  runtime.ui_storage = std::make_unique<OverlayDomainStorage>(*runtime.storage);
  runtime.model_domain =
      std::make_unique<ae::Domain>(ae::Now(), *runtime.storage);
  runtime.ui_domain =
      std::make_unique<ae::Domain>(ae::Now(), *runtime.ui_storage);
  runtime.application = LoadApplication<Application>(
      *runtime.model_domain, ae::ObjId{ToObjId(ChatObjId::Application)});
  ResetRuntimePresenceState(*runtime.application);
  return runtime;
}

}  // namespace apptraverse
