#include "win_app.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "apptraverse/distill.h"
#include "apptraverse/graph_mirror.h"
#include "apptraverse/shared_frame_codec.h"

#include "chat_commands.h"
#include "chat_connection_ui_state.h"
#include "chat_ids.h"
#include "chat_log.h"
#include "chat_presence.h"
#include "win_util.h"

namespace apptraverse {
namespace {

enum class UiNotifyKind : WPARAM {
  ConnectionReady = 1,
  ConnectionDisconnected = 2,
  RuntimeDiag = 3,
};

LRESULT CALLBACK DispatcherProc(HWND hwnd, UINT msg, WPARAM wparam,
                                LPARAM lparam) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  auto* app =
      reinterpret_cast<WinChatApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (app == nullptr) {
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  if (msg == WM_APPTRAVERSE_PUBLISHED) {
    app->OnPublished(static_cast<std::uint32_t>(wparam),
                     reinterpret_cast<PublicationChannel<3>*>(lparam));
    return 0;
  }
  if (msg == WM_APPTRAVERSE_CONNECTION_UI) {
    if (wparam == static_cast<WPARAM>(UiNotifyKind::ConnectionReady)) {
      app->OnUiConnectionReady();
    } else if (wparam ==
               static_cast<WPARAM>(UiNotifyKind::ConnectionDisconnected)) {
      app->OnUiConnectionDisconnected();
    }
    return 0;
  }
  if (msg == WM_APPTRAVERSE_RUNTIME_DIAG) {
    app->OnUiRuntimeDiag();
    return 0;
  }
  if (msg == WM_TIMER) {
    app->PollPresenceTestHooks();
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

void WinChatApp::OnUiConnectionReady() {
  if (presentation_) {
    presentation_->NotifyPeerReady();
  }
}

void WinChatApp::OnUiConnectionDisconnected() {
  if (presentation_) {
    presentation_->NotifyPeerDisconnected();
  }
}

void WinChatApp::OnUiRuntimeDiag() {
  if (!presentation_ || !pending_diag_valid_) {
    return;
  }
  presentation_->ApplyRuntimeDiag(pending_diag_);
}

void WinChatApp::PostConnectionUiReady() {
  if (dispatcher_ != nullptr) {
    PostMessageW(dispatcher_, WM_APPTRAVERSE_CONNECTION_UI,
                 static_cast<WPARAM>(UiNotifyKind::ConnectionReady), 0);
  }
}

void WinChatApp::PostConnectionUiDisconnected() {
  if (dispatcher_ != nullptr) {
    PostMessageW(dispatcher_, WM_APPTRAVERSE_CONNECTION_UI,
                 static_cast<WPARAM>(UiNotifyKind::ConnectionDisconnected), 0);
  }
}

void WinChatApp::PostRuntimeDiagFromModelThread() {
#ifndef NDEBUG
  ChatRuntimeDiagUiState diag;
  if (runtime_.application && runtime_.application->chat_room.is_valid()) {
    diag.journal_count = runtime_.application->chat_room->journal.size();
  }
  diag.pending_count = CountSharedPendingAndInFlight(shared_);
  diag.peer_connected = false;
  for (auto const& peer : shared_.instance.peers) {
    if (peer.channel_ready) {
      diag.peer_connected = true;
      break;
    }
  }
  pending_diag_ = diag;
  pending_diag_valid_ = true;
  if (dispatcher_ != nullptr) {
    PostMessageW(dispatcher_, WM_APPTRAVERSE_RUNTIME_DIAG, 0, 0);
  }
#endif
}
void WinChatApp::OnPublished(std::uint32_t root_id,
                             PublicationChannel<3>* channel) {
  ApplyPublication(root_id, channel);
}

void WinChatApp::ApplyPublication(std::uint32_t root_id,
                                  PublicationChannel<3>* channel) {
  if (exiting_) {
    if (channel->TakePublished() != nullptr) {
      channel->ReleaseConsumer();
    }
    return;
  }
  auto applied = ui_mirror_->ApplyPublished(*channel, root_id);
  if (applied.root_id == 0) {
    return;
  }
  auto const chat_id = chat::ToObjId(chat::ChatObjId::ChatRoom);
  auto const aether_id = chat::ToObjId(chat::ChatObjId::LocalAetherIdentity);
  if (presentation_ && (applied.root_id == chat_id ||
                        applied.root_id == aether_id)) {
    if (applied.root_id == chat_id &&
        runtime_.ui_application->chat_room->clients.size() > 0) {
      std::string contacts;
      for (auto const& ui_client : runtime_.ui_application->chat_room->clients) {
        if (!ui_client.is_valid()) {
          continue;
        }
        ui_client.Load();
        if (!contacts.empty()) {
          contacts += ',';
        }
        contacts += ui_client->AetherUidText();
        contacts += ':';
        contacts += PresenceStateName(ui_client->GetPresence());
      }
      chat::ChatLog("UI_PRESENCE contacts=" + contacts);
    }
    presentation_->PresentChatWindow();
  }
  chat::ChatLog("ui apply root=" + std::to_string(applied.root_id) +
                " changed=" + std::to_string(applied.changed_obj_ids.size()));
}

void WinChatApp::TickDelivery() {
  auto const now = std::chrono::steady_clock::now();
  if (last_delivery_tick_.time_since_epoch().count() != 0 &&
      now - last_delivery_tick_ < std::chrono::milliseconds{50}) {
    return;
  }
  last_delivery_tick_ = now;
  TickSharedDelivery(shared_, now, shared_transport_.get());
  PostRuntimeDiagFromModelThread();
}

void WinChatApp::OnPeerReady(std::string remote_uid) {
  if (!model_runtime_) {
    return;
  }
  model_runtime_->Post([this, remote_uid = std::move(remote_uid)] {
    auto* peer = shared_.instance.FindPeer(remote_uid);
    bool const was_ready = peer != nullptr && peer->channel_ready;
    SetSharedPeerChannelReady(shared_, remote_uid, true);
    MonitorRemoteOnce(remote_uid);
    if (!was_ready) {
      TickSharedDelivery(shared_, std::chrono::steady_clock::now(),
                         shared_transport_.get());
    }
    PostConnectionUiReady();
    PostRuntimeDiagFromModelThread();
  });
}

void WinChatApp::OnPeerClosed(std::string remote_uid) {
  if (!model_runtime_) {
    return;
  }
  model_runtime_->Post([this, remote_uid = std::move(remote_uid)] {
    SetSharedPeerChannelReady(shared_, remote_uid, false);
    PostConnectionUiDisconnected();
    PostRuntimeDiagFromModelThread();
  });
}

void WinChatApp::OnPeerWriteFailed(std::string remote_uid) {
  if (!model_runtime_) {
    return;
  }
  model_runtime_->Post([this, remote_uid = std::move(remote_uid)] {
    (void)remote_uid;
  });
}

void WinChatApp::OnPeerFrame(std::string remote_uid,
                             std::vector<std::uint8_t> bytes) {
  if (!model_runtime_) {
    return;
  }
  model_runtime_->Post([this, remote_uid = std::move(remote_uid),
                        bytes = std::move(bytes)]() mutable {
    HandlePeerFrameOnModelThread(std::move(remote_uid), std::move(bytes));
  });
}

void WinChatApp::OnPeerPresence(std::string remote_uid, PresenceState state) {
  if (!model_runtime_) {
    return;
  }
  model_runtime_->Post([this, remote_uid = std::move(remote_uid), state] {
    auto const was_online =
        PresenceIsOnline(shared_.presence.Remote(remote_uid));
    SetSharedPeerPresence(shared_, remote_uid, state);
    if (!was_online && PresenceIsOnline(state)) {
      TickSharedDelivery(shared_, std::chrono::steady_clock::now(),
                         shared_transport_.get());
    }
  });
}

void WinChatApp::MonitorRemoteOnce(std::string const& remote_uid) {
  if (remote_uid.empty() ||
      remote_uid == shared_.instance.local_aether_uid) {
    return;
  }
  EnsureSharedPeer(shared_, remote_uid);
  if (runtime_.application && runtime_.application->chat_room.is_valid()) {
    auto contact =
        EnsurePresenceContact(*runtime_.application->chat_room, remote_uid);
    if (contact.is_valid() && model_runtime_) {
      model_runtime_->AttachNode(*contact, *runtime_.application->chat_room);
    }
    ApplyPresenceOverlay(shared_);
  }
  if (!monitored_remote_uids_.insert(remote_uid).second) {
    return;
  }
  aether_runtime_.MonitorPeerPresence(remote_uid);
}

void WinChatApp::PollPresenceTestHooks() {
  // Test-only hook: scripts/run_chat_presence_ab.ps1 writes peer UIDs to
  // monitor_peer_uid.txt so Host can start remote Presence monitoring without
  // relying on inbound P2P accept (Presence is independent of transport).
  if (state_dir_.empty() || !model_runtime_) {
    return;
  }
  auto const path = state_dir_ / "monitor_peer_uid.txt";
  if (!std::filesystem::exists(path)) {
    return;
  }
  std::ifstream in{path};
  std::string uid;
  std::getline(in, uid);
  while (!uid.empty() &&
         (uid.back() == '\r' || uid.back() == '\n' || uid.back() == ' ')) {
    uid.pop_back();
  }
  if (uid.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::remove(path, ec);
  model_runtime_->Post([this, uid = std::move(uid)] {
    MonitorRemoteOnce(uid);
  });
}

void WinChatApp::HandlePeerFrameOnModelThread(
    std::string remote_uid, std::vector<std::uint8_t> bytes) {
  SharedEventFrame event_frame;
  if (DecodeSharedEventFrame(bytes, event_frame)) {
    auto const apply = ApplyIncomingSharedEvent(
        shared_, remote_uid, event_frame,
        [this](std::string const& client_uid) {
          if (client_uid.empty() ||
              client_uid == shared_.instance.local_aether_uid) {
            return;
          }
          EnsureSharedPeer(shared_, client_uid);
        },
        [this](ChatClient& client) {
          model_runtime_->AttachNode(client,
                                     *runtime_.application->chat_room);
        });
    if (SharedApplyResultAllowsAck(apply)) {
      SendSharedAck(shared_, shared_transport_.get(), remote_uid,
                    event_frame.event_id);
    }
    TickSharedDelivery(shared_, std::chrono::steady_clock::now(),
                       shared_transport_.get());
    return;
  }
  SharedAckFrame ack_frame;
  if (DecodeSharedAckFrame(bytes, ack_frame)) {
    HandleSharedAck(shared_, remote_uid, ack_frame);
    // Immediately continue journal transfer in the same Model turn.
    TickSharedDelivery(shared_, std::chrono::steady_clock::now(),
                       shared_transport_.get());
  }
}

void WinChatApp::RequestExit() {
  if (exiting_) {
    return;
  }
  exiting_ = true;
  aether_runtime_.RequestStop();
  if (model_runtime_) {
    model_runtime_->RequestStop();
  }
  if (presentation_) {
    presentation_->Destroy();
  }
  PostQuitMessage(0);
}

int WinChatApp::Run(std::filesystem::path const& state_dir, ChatRole role,
                    std::string connect_host_uid) {
  pending_connect_host_uid_ = std::move(connect_host_uid);
  state_dir_ = state_dir;
  EnsureChatRegistration();
  EnsureChatPresenterRegistration();
  chat::SetChatLogPath((state_dir / "chat_runtime.log").string());
  runtime_ = LoadChatModel(state_dir);
  SetApplicationRole(*runtime_.application, role);
  chat::ChatLog(
      "HOST_OBJ host_client=" +
      std::to_string(runtime_.application->host_client.id().id()) +
      " room_client0=" +
      (runtime_.application->chat_room->clients.empty()
           ? std::string{"none"}
           : std::to_string(
                 runtime_.application->chat_room->clients[0].id().id())));
  auto ui_root = CopyModelGraphToUiDomain(*runtime_.application,
                                          *runtime_.ui_domain,
                                          *runtime_.ui_storage);
  runtime_.ui_application = Application::ptr::MakeFromThis(
      static_cast<Application*>(ui_root.get()));
  ui_thread_ = GetCurrentThreadId();

  WNDCLASSW wc{};
  wc.lpfnWndProc = &DispatcherProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"AppTraverseChatUiDispatcher";
  RegisterClassW(&wc);
  dispatcher_ = CreateWindowExW(0, L"AppTraverseChatUiDispatcher", L"", 0, 0, 0,
                                0, 0, HWND_MESSAGE, nullptr,
                                GetModuleHandleW(nullptr), this);
  assert(dispatcher_ != nullptr);
  SetTimer(dispatcher_, /*nIDEvent=*/1, /*uElapse=*/250, nullptr);

  auto notify = [this](std::uint32_t root_id, PublicationChannel<3>* channel) {
    if (GetCurrentThreadId() == ui_thread_) {
      ApplyPublication(root_id, channel);
      return;
    }
    PostMessageW(dispatcher_, WM_APPTRAVERSE_PUBLISHED,
                 static_cast<WPARAM>(root_id),
                 reinterpret_cast<LPARAM>(channel));
  };

  ui_mirror_ = std::make_unique<UiMirror>(*runtime_.ui_domain,
                                          *runtime_.ui_storage, notify);
  model_runtime_ =
      std::make_unique<ModelRuntime>(*runtime_.application, *ui_mirror_);
  model_runtime_->AddPresentationRoot(*runtime_.application->chat_room);
  model_runtime_->AddPresentationRoot(*runtime_.application->local_aether);
  shared_transport_ = std::make_unique<AetherSharedTransport>(aether_runtime_);

  auto const chat_room_id = chat::ToObjId(chat::ChatObjId::ChatRoom);
  model_runtime_->SetUpdateObserver([this, chat_room_id](Node& node) {
    if (node.obj_id.id() == chat_room_id) {
      TickDelivery();
    }
  });

  presentation_ = LoadApplication<WinChatPresentationApplication>(
      *runtime_.ui_domain,
      ae::ObjId{chat::ToObjId(chat::ChatObjId::WinPresentationApplication)});
  presentation_->chat_window->application = runtime_.ui_application;
  presentation_->chat_window->room = runtime_.ui_application->chat_room;
  presentation_->chat_window->identity = runtime_.ui_application->local_aether;
  presentation_->latency_tracker = &latency_tracker_;
  presentation_->on_close = [this] { RequestExit(); };
  presentation_->on_chat_send = [this](ChatSendUiRequest request) {
    model_runtime_->Post([this, request = std::move(request)]() mutable {
      auto result = CommitLocalMessage(shared_, *runtime_.application->host_client,
                                       std::move(request.text),
                                       request.sent_at_unix_ms);
      if (result.committed && request.ui_trace_id != 0) {
        latency_tracker_.BindEvent(request.ui_trace_id,
                                   result.local_event_obj_id);
      } else if (!result.committed && request.ui_trace_id != 0) {
        latency_tracker_.Cancel(request.ui_trace_id);
      }
      TickSharedDelivery(shared_, std::chrono::steady_clock::now(),
                         shared_transport_.get());
    });
  };
  presentation_->on_connect_host = [this](std::string host_uid) {
    model_runtime_->Post([this, host_uid = std::move(host_uid)] {
      ConnectToHostCommand(shared_, std::move(host_uid),
                           [this](std::string const& peer_uid) {
                             aether_runtime_.OpenPeer(peer_uid);
                           });
    });
  };
  presentation_->OnLoad();
  presentation_->PresentChatWindow();

  model_runtime_->Start();

  aether_runtime_.SetPeerCallbacks(
      [this](std::string remote_uid) { OnPeerReady(std::move(remote_uid)); },
      [this](std::string remote_uid) { OnPeerClosed(std::move(remote_uid)); },
      [this](std::string remote_uid, std::vector<std::uint8_t> bytes) {
        OnPeerFrame(std::move(remote_uid), std::move(bytes));
      });
  aether_runtime_.SetPeerWriteFailedCallback(
      [this](std::string remote_uid) {
        OnPeerWriteFailed(std::move(remote_uid));
      });
  aether_runtime_.SetPeerPresenceCallback(
      [this](std::string remote_uid, PresenceState state) {
        OnPeerPresence(std::move(remote_uid), state);
      });

  auto aether_dir = state_dir / "aether";
  aether_runtime_.Start(
      aether_dir,
      [this](std::string uid_text) {
        model_runtime_->Post([this, uid_text = std::move(uid_text)] {
          SetLocalAetherUidText(*runtime_.application->local_aether,
                                std::move(uid_text));
          InitializeChatSharedBinding(shared_, *runtime_.application,
                                      runtime_.application->local_aether
                                          ->UidTextBytes());
          if (runtime_.application->chat_room->journal.empty()) {
            CommitLocalJoin(shared_, *runtime_.application->host_client);
          }
          // Map room members onto the ChatRoom presentation root so presence
          // SetPresence / overlay updates publish to the contacts list.
          for (auto const& client : runtime_.application->chat_room->clients) {
            if (!client.is_valid()) {
              continue;
            }
            model_runtime_->AttachNode(*client,
                                       *runtime_.application->chat_room);
          }
          if (!pending_connect_host_uid_.empty()) {
            auto host_uid = std::move(pending_connect_host_uid_);
            pending_connect_host_uid_.clear();
            ConnectToHostCommand(shared_, std::move(host_uid),
                                 [this](std::string const& peer_uid) {
                                   aether_runtime_.OpenPeer(peer_uid);
                                 });
          }
          ApplyPresenceOverlay(shared_);
          for (auto const& peer : shared_.instance.peers) {
            MonitorRemoteOnce(peer.remote_aether_uid);
          }
          for (auto const& client : runtime_.application->chat_room->clients) {
            if (!client.is_valid()) {
              continue;
            }
            auto const uid = client->AetherUidText();
            if (!uid.empty() && uid != shared_.instance.local_aether_uid) {
              MonitorRemoteOnce(uid);
            }
          }
        });
      },
      [this](PresenceState state) {
        model_runtime_->Post([app = &*runtime_.application, state, this] {
          SetHostClientPresence(*app->host_client, state);
          shared_.presence.SetLocalSelf(state);
          if (app->host_client.is_valid()) {
            model_runtime_->AttachNode(*app->host_client, *app->chat_room);
          }
          ApplyPresenceOverlay(shared_);
          std::string contacts;
          for (auto const& client : app->chat_room->clients) {
            if (!client.is_valid()) {
              continue;
            }
            if (!contacts.empty()) {
              contacts += ',';
            }
            contacts += client->AetherUidText();
            contacts += ':';
            contacts += PresenceStateName(client->GetPresence());
          }
          chat::ChatLog(std::string{"MODEL_PRESENCE local="} +
                        PresenceStateName(state) + " room_clients=" +
                        std::to_string(app->chat_room->clients.size()) +
                        " contacts=" + contacts);
        });
      });

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  RequestExit();
  aether_runtime_.Join();
  if (dispatcher_ != nullptr) {
    DestroyWindow(dispatcher_);
    dispatcher_ = nullptr;
  }
  if (model_runtime_) {
    model_runtime_->RequestStop();
    model_runtime_->Join();
  }
  ResetRuntimePresenceState(*runtime_.application);
  SaveDistilledRoot(*runtime_.application);  // runtime-save-ok: shutdown
  return static_cast<int>(msg.wParam);
}

}  // namespace apptraverse
