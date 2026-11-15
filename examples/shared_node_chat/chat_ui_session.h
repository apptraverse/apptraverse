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
          ChatSession::FlushProfile flush_transport={},
          std::filesystem::path transport_profile={});
  ~Session();
  Session(Session const&)=delete;
  void ConfigurePush(std::string service_uid);
  void RegisterPushToken(std::string token, bool apple = false);
  void Start();
  void Stop();
  void Checkpoint(std::function<void(bool)> completion);
  // Main/UI thread after the first publication; RAM only. Browser lifecycle
  // handlers commit these bytes directly without waiting for filesystem I/O.
  // Do not overlap with Checkpoint/Stop, which can perform filesystem I/O.
  bool CaptureUser(std::vector<std::uint8_t>& bytes);
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
