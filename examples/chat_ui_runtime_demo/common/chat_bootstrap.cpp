#include "chat_bootstrap.h"

#include <cassert>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

#include "apptraverse/runtime_lifecycle.h"
#include "chat_ids.h"
#include "chat_log.h"

namespace chat {
namespace {

bool DirectoryHasEntries(std::filesystem::path const& dir) {
  if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
    return false;
  }
  return std::filesystem::directory_iterator{dir} !=
         std::filesystem::directory_iterator{};
}

}  // namespace

ChatApplication::ptr BuildChatGraph(ae::Domain& domain,
                                    ChatCreateOptions options) {
  if (options.display_name.empty()) {
    options.display_name = kDefaultHostName;
  }

  auto application = ChatApplication::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ChatObjId::Application)));
  auto room = ChatRoom::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ChatObjId::ChatRoom)));
  auto runtime = ApplicationRuntimeState::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ChatObjId::ApplicationRuntime)));
  auto network = NetworkState::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ChatObjId::NetworkState)));
  auto aether = AetherRegistrationState::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ChatObjId::AetherRegistration)));
  auto display_name = ImmutableString::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ChatObjId::LocalDisplayName)));
  display_name->bytes = std::move(options.display_name);

  application->room = room;
  application->runtime = runtime;
  application->network = network;
  application->aether = aether;
  application->local_display_name = display_name;
  application->SetRole(options.role);
  return application;
}

ChatApplication::ptr BuildChatGraph(ae::Domain& domain,
                                    std::string display_name) {
  ChatCreateOptions options;
  options.display_name = std::move(display_name);
  return BuildChatGraph(domain, std::move(options));
}

bool HasChatApplicationState(std::filesystem::path const& dir) {
  if (!std::filesystem::exists(dir)) {
    return false;
  }
  auto const app_dir =
      dir / std::to_string(ToObjId(ChatObjId::Application));
  return std::filesystem::exists(app_dir);
}

void DistillChatModel(std::filesystem::path const& dir,
                      std::string display_name) {
  std::filesystem::remove_all(dir);
  apptraverse::DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = BuildChatGraph(domain, std::move(display_name));
  apptraverse::FinalizeDistilledGraph(*application);
  apptraverse::SaveDistilledRoot(*application);  // runtime-save-ok: distill
  ChatLog("distilled chat model to " + dir.string());
}

ChatRuntime LoadChatModel(std::filesystem::path const& dir) {
  if (!HasChatApplicationState(dir)) {
    throw std::runtime_error("application state missing: " + dir.string());
  }

  ChatRuntime runtime;
  runtime.storage =
      std::make_unique<apptraverse::DirectoryDomainStorage>(dir);
  runtime.ui_storage =
      std::make_unique<apptraverse::OverlayDomainStorage>(*runtime.storage);
  runtime.model_domain = std::make_unique<ae::Domain>(*runtime.storage);
  runtime.ui_domain = std::make_unique<ae::Domain>(*runtime.ui_storage);
  runtime.application = apptraverse::LoadApplication<ChatApplication>(
      *runtime.model_domain, ae::ObjId{ToObjId(ChatObjId::Application)});
  return runtime;
}

ChatRuntime CreateOrLoadChatModel(std::filesystem::path const& dir,
                                  ChatCreateOptions options) {
  if (HasChatApplicationState(dir)) {
    ChatLog("LOAD_EXISTING_STATE dir=" + dir.string() +
            " (CLI role/name ignored)");
    return LoadChatModel(dir);
  }
  if (DirectoryHasEntries(dir) && !HasChatApplicationState(dir)) {
    ChatLog("CREATE_GRAPH_IN_EXISTING_DIR dir=" + dir.string());
  } else {
    ChatLog("CREATE_NEW_STATE dir=" + dir.string());
  }
  std::filesystem::create_directories(dir);
  {
    apptraverse::DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto application = BuildChatGraph(domain, std::move(options));
    apptraverse::FinalizeDistilledGraph(*application);
    apptraverse::SaveDistilledRoot(*application);  // runtime-save-ok: first start
    assert(application->room.is_valid());
    assert(application->runtime.is_valid());
    assert(application->network.is_valid());
    assert(application->aether.is_valid());
    assert(!application->local_client.is_valid());
    assert(application->room->clients.empty());
    assert(application->aether->GetPhase() ==
           apptraverse::AetherRegistrationPhase::kRegistering);
  }
  return LoadChatModel(dir);
}

}  // namespace chat
