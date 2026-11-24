#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include "apptraverse/model_object_proxy.h"
#include "chat_ui_model.h"
namespace apptraverse::chat::gui {
// Chat business/UI state stays in RAM. Only native UI LOCAL is persisted;
// SDK identity belongs to the independent transport session under
// profile/transport.
class Session {
 public:
  Session(std::filesystem::path profile, std::string name, bool host,
          std::string initial_host, std::function<void()> changed,
          bool load_only = false,
          ChatSession::FlushProfile flush_transport = {},
          std::filesystem::path transport_profile = {});
  ~Session();
  Session(Session const&) = delete;
  void ConfigurePush(std::string service_uid);
  void RegisterPushToken(std::string token, bool apple = false);
  void RegisterWebPush(std::string endpoint);
  // Capture native settings on the UI thread before Start; the model applies
  // this session's baseline after loading LOCAL and before first publication.
  void PrepareDeviceEnvironment(ui::UiDeviceEnvironment environment);
  // Entry points register the chat graph and configure their presenter
  // factories before starting any session.
  void Start();
  void Stop();
  void Checkpoint(std::function<void(bool)> completion);
  // Main/UI thread after the first publication; RAM only. Browser lifecycle
  // handlers commit these bytes directly without waiting for filesystem I/O.
  // Do not overlap with Checkpoint/Stop, which can perform filesystem I/O.
  // Legacy portable capture returns an empty native storage table.
  bool CaptureUser(std::vector<std::uint8_t>& bytes);
  bool CaptureLocal(std::vector<std::uint8_t>& bytes,
                    std::filesystem::path* output_file = nullptr);
  // GUI thread only: consume publication, then activate/update standard
  // presenters.
  bool ApplyPublication(Presentation& host);
  bool ApplyPublication(void* native_host);
  void RemountPresentation();
  ChatUi const* Mirror() const;
  // Runtime transport diagnostics are value copies, never serialized UI state.
  ChatView TransportView() const;
  ModelObjectProxy& Proxy();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
#ifdef APPTRAVERSE_CHAT_UI_PRODUCER
int ExportUiSupply(std::filesystem::path const& path);
#endif
}  // namespace apptraverse::chat::gui
