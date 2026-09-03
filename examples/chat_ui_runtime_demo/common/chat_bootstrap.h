#ifndef APPTRAVERSE_CHAT_BOOTSTRAP_H_
#define APPTRAVERSE_CHAT_BOOTSTRAP_H_

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

namespace apptraverse {

struct ChatRuntime {
  std::unique_ptr<DirectoryDomainStorage> storage;
  std::unique_ptr<OverlayDomainStorage> ui_storage;
  std::unique_ptr<ae::Domain> model_domain;
  std::unique_ptr<ae::Domain> ui_domain;
  Application::ptr application;
  Application::ptr ui_application;
};

Application::ptr BuildChatGraph(
    ae::Domain& domain,
    std::string host_name = std::string{chat::kDefaultHostName});
void CommitHostJoin(Application& application);
void DistillChatModel(
    std::filesystem::path const& dir,
    std::string host_name = std::string{chat::kDefaultHostName});
ChatRuntime LoadChatModel(std::filesystem::path const& dir);

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_BOOTSTRAP_H_
