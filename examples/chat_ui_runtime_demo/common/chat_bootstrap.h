#ifndef CHAT_BOOTSTRAP_H_
#define CHAT_BOOTSTRAP_H_

#include <filesystem>
#include <memory>
#include <string>

#include "aether/clock.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/overlay_domain_storage.h"
#include "chat_ids.h"
#include "chat_model.h"

namespace chat {

struct ChatCreateOptions {
  ChatRole role{ChatRole::Host};
  std::string display_name;
};

struct ChatRuntime {
  std::unique_ptr<apptraverse::DirectoryDomainStorage> storage;
  std::unique_ptr<apptraverse::OverlayDomainStorage> ui_storage;
  std::unique_ptr<ae::Domain> model_domain;
  std::unique_ptr<ae::Domain> ui_domain;
  ChatApplication::ptr application;
  ChatApplication::ptr ui_application;
};

ChatApplication::ptr BuildChatGraph(
    ae::Domain& domain, ChatCreateOptions options = {});
ChatApplication::ptr BuildChatGraph(ae::Domain& domain, std::string display_name);

bool HasChatApplicationState(std::filesystem::path const& dir);

// Test helper: wipe dir and write a fresh unregistered graph.
void DistillChatModel(std::filesystem::path const& dir,
                      std::string display_name = std::string{kDefaultHostName});

ChatRuntime LoadChatModel(std::filesystem::path const& dir);

// Ordinary startup: load existing application state, or create it. Never
// deletes a non-empty directory.
ChatRuntime CreateOrLoadChatModel(std::filesystem::path const& dir,
                                  ChatCreateOptions options);

}  // namespace chat

#endif  // CHAT_BOOTSTRAP_H_
