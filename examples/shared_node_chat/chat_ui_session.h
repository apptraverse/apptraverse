#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include "chat_ui_model.h"
#include "apptraverse/model_object_proxy.h"
namespace apptraverse::chat::gui {
// UI USER belongs to this model session. SharedNode replication and SDK identity
// belong to the independent transport session under profile/transport.
class Session {
 public:
  Session(std::filesystem::path profile,std::string name,bool host,
          std::string initial_host,std::function<void()> changed,bool load_only=false,
          ChatSession::FlushProfile flush_transport={});
  ~Session();
  Session(Session const&)=delete;
  void Start();
  void Stop();
  void Checkpoint(std::function<void(bool)> completion);
  // GUI thread only: consume publication, then activate/update standard presenters.
  bool ApplyPublication(Presentation& host);
  ChatUi const* Mirror()const;
  // Runtime transport diagnostics are value copies, never serialized UI state.
  ChatView TransportView()const;
  ModelObjectProxy& Proxy();
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
#ifdef APPTRAVERSE_CHAT_UI_PRODUCER
int ExportUiSupply(std::filesystem::path const& path);
#endif
} // namespace apptraverse::chat::gui
