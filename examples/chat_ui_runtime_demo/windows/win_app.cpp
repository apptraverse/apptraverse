#include "win_app.h"

#include <cassert>
#include <filesystem>
#include <string>
#include <utility>

#include "apptraverse/distill.h"
#include "apptraverse/graph_mirror.h"

#include "chat_commands.h"
#include "chat_ids.h"
#include "chat_log.h"
#include "chat_presence.h"
#include "apptraverse/runtime_lifecycle.h"
#include "win_util.h"

namespace chat::win32 {
namespace {

using apptraverse::PublicationChannel;

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
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

bool HasPresentationState(std::filesystem::path const& dir) {
  auto const pres_dir =
      dir / std::to_string(ToObjId(ChatObjId::WinPresentationApplication));
  return std::filesystem::exists(pres_dir);
}

}  // namespace

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
  auto const chat_id = ToObjId(ChatObjId::ChatRoom);
  auto const net_id = ToObjId(ChatObjId::NetworkState);
  auto const aether_id = ToObjId(ChatObjId::AetherRegistration);
  if (presentation_ &&
      (applied.root_id == chat_id || applied.root_id == net_id ||
       applied.root_id == aether_id)) {
    if (applied.root_id == chat_id &&
        runtime_.ui_application->room->clients.size() > 0) {
      std::string contacts;
      for (auto const& ui_client : runtime_.ui_application->room->clients) {
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
        contacts += '@';
        contacts += std::to_string(ui_client->Generation());
      }
      ChatLog("UI_PRESENCE contacts=" + contacts + " room_gen=" +
              std::to_string(runtime_.ui_application->room->Generation()));
    }
    presentation_->PresentChatWindow();
  }
  ChatLog("ui apply root=" + std::to_string(applied.root_id) +
          " changed=" + std::to_string(applied.changed_obj_ids.size()));
}

void WinChatApp::HandleAetherUidOnModelThread(std::string uid_text) {
  auto& app = *runtime_.application;
  bool const created =
      CompleteLocalRegistration(app, uid_text, model_runtime_.get());
  ChatLog(std::string{"MODEL_REGISTRATION uid="} + uid_text +
          " created=" + (created ? "1" : "0") + " phase=" +
          std::to_string(static_cast<int>(app.aether->GetPhase())) +
          " room_clients=" + std::to_string(app.room->clients.size()));
  aether_runtime_.EnableLocalPresenceMonitoring();
}

void WinChatApp::HandleNetworkObservationOnModelThread(
    apptraverse::NetworkAvailability availability) {
  auto& app = *runtime_.application;
  if (!app.network.is_valid() || !app.runtime.is_valid()) {
    return;
  }
  auto const run = app.runtime->run_id;
  bool committed = false;
  switch (availability) {
    case apptraverse::NetworkAvailability::kInterfaceUnavailable:
      committed = apptraverse::CommitNetworkInterfaceUnavailable(*app.network,
                                                                 run);
      break;
    case apptraverse::NetworkAvailability::kInternetUnavailable:
      committed = apptraverse::CommitInternetUnavailable(*app.network, run);
      break;
    case apptraverse::NetworkAvailability::kAvailable:
      committed = apptraverse::CommitNetworkAvailable(*app.network, run);
      break;
    case apptraverse::NetworkAvailability::kInitializing:
      committed = apptraverse::CommitNetworkInitializing(*app.network, run);
      break;
  }
  ChatLog(std::string{"MODEL_NETWORK availability="} +
          std::to_string(static_cast<int>(availability)) +
          " committed=" + (committed ? "1" : "0"));
}

void WinChatApp::HandleAetherFailedOnModelThread(std::string error) {
  ChatLog("MODEL_REGISTRATION_FAILED " + error);
  HandleNetworkObservationOnModelThread(
      apptraverse::NetworkAvailability::kInternetUnavailable);
}

void WinChatApp::HandlePresenceOnModelThread(PresenceState state) {
  auto& app = *runtime_.application;
  if (!app.local_client.is_valid()) {
    return;
  }
  if (state != PresenceState::kOnline && state != PresenceState::kOffline) {
    return;
  }
  PresenceState const old_state = app.local_client->GetPresence();
  bool const materialized = CommitPresenceChanged(*app.local_client, state);
  ChatLog(std::string{"MODEL_PRESENCE local="} + PresenceStateName(state) +
          " old=" + PresenceStateName(old_state) +
          " materialized=" + (materialized ? "1" : "0") +
          " via=PresenceChangedEvent client_gen=" +
          std::to_string(app.local_client->Generation()) + " room_gen=" +
          std::to_string(app.room->Generation()));
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

int WinChatApp::Run(std::filesystem::path const& state_dir,
                    ChatCreateOptions create) {
  state_dir_ = state_dir;
  EnsureChatRegistration();
  EnsureChatPresenterRegistration();
  ChatLog("CHAT_RUNTIME_LOG " + (state_dir / "chat_runtime.log").string());
  SetChatLogPath((state_dir / "chat_runtime.log").string());
  BeginChatSession();
  runtime_ = CreateOrLoadChatModel(state_dir, std::move(create));
  BeginCurrentRun(*runtime_.application);
  ChatLog(std::string{"APP_OBJ role="} +
          (runtime_.application->GetRole() == ChatRole::Host ? "host"
                                                             : "client") +
          " name=" + runtime_.application->LocalDisplayNameBytes() +
          " net=" +
          std::to_string(static_cast<int>(
              runtime_.application->network->GetAvailability())) +
          " aether=" +
          std::to_string(static_cast<int>(
              runtime_.application->aether->GetPhase())) +
          " room_clients=" +
          std::to_string(runtime_.application->room->clients.size()));

  auto ui_root = apptraverse::CopyModelGraphToUiDomain(
      *runtime_.application, *runtime_.ui_domain, *runtime_.ui_storage);
  runtime_.ui_application = ChatApplication::ptr::MakeFromThis(
      static_cast<ChatApplication*>(ui_root.get()));
  ui_thread_ = GetCurrentThreadId();

  WNDCLASSW wc{};
  wc.lpfnWndProc = &DispatcherProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"AppTraverseChatUiDispatcher";
  if (RegisterClassW(&wc) == 0) {
    auto const err = GetLastError();
    if (err != ERROR_CLASS_ALREADY_EXISTS) {
      ChatLog("DISPATCHER_REGISTER_FAILED err=" + std::to_string(err));
    }
  }
  dispatcher_ = CreateWindowExW(0, L"AppTraverseChatUiDispatcher", L"", 0, 0, 0, 0,
                                0, HWND_MESSAGE, nullptr,
                                GetModuleHandleW(nullptr), this);
  if (dispatcher_ == nullptr) {
    ChatLog("DISPATCHER_CREATE_FAILED err=" + std::to_string(GetLastError()));
    return 3;
  }

  auto notify = [this](std::uint32_t root_id, PublicationChannel<3>* channel) {
    if (GetCurrentThreadId() == ui_thread_) {
      ApplyPublication(root_id, channel);
      return;
    }
    PostMessageW(dispatcher_, WM_APPTRAVERSE_PUBLISHED,
                 static_cast<WPARAM>(root_id),
                 reinterpret_cast<LPARAM>(channel));
  };

  ui_mirror_ = std::make_unique<apptraverse::UiMirror>(
      *runtime_.ui_domain, *runtime_.ui_storage, notify);
  model_runtime_ = std::make_unique<apptraverse::ModelRuntime>(
      *runtime_.application, *ui_mirror_);
  model_runtime_->AddPresentationRoot(*runtime_.application->room);
  model_runtime_->AddPresentationRoot(*runtime_.application->network);
  model_runtime_->AddPresentationRoot(*runtime_.application->aether);

  if (HasPresentationState(state_dir)) {
    presentation_ = apptraverse::LoadApplication<WinChatPresentationApplication>(
        *runtime_.ui_domain,
        ae::ObjId{ToObjId(ChatObjId::WinPresentationApplication)});
  } else {
    presentation_ =
        BuildPresentationGraph(*runtime_.ui_domain, *runtime_.ui_application);
    apptraverse::SaveDistilledRoot(*presentation_);  // runtime-save-ok: first start
  }
  presentation_->chat_window->application = runtime_.ui_application;
  presentation_->chat_window->room = runtime_.ui_application->room;
  presentation_->chat_window->network = runtime_.ui_application->network;
  presentation_->chat_window->aether = runtime_.ui_application->aether;
  presentation_->latency_tracker = &latency_tracker_;
  presentation_->on_close = [this] { RequestExit(); };
  presentation_->on_chat_send = [this](ChatSendUiRequest request) {
    model_runtime_->Post([this, request = std::move(request)]() mutable {
      if (!runtime_.application->local_client.is_valid()) {
        return;
      }
      static_cast<void>(CommitSendChatMessage(
          *runtime_.application->room, *runtime_.application->local_client,
          std::move(request.text), request.sent_at_unix_ms));
    });
  };
  presentation_->on_join_room = [] {
    ChatLog("JOIN_ROOM action unimplemented");
  };
  presentation_->OnLoad();
  presentation_->PresentChatWindow();

  model_runtime_->Start();

  auto aether_dir = state_dir / "aether";
  aether_runtime_.Start(
      aether_dir,
      [this](std::string uid_text) {
        model_runtime_->Post([this, uid_text = std::move(uid_text)] {
          HandleAetherUidOnModelThread(std::move(uid_text));
        });
      },
      [this](PresenceState state) {
        model_runtime_->Post([this, state] {
          HandlePresenceOnModelThread(state);
        });
      },
      [this](std::string error) {
        model_runtime_->Post([this, error = std::move(error)] {
          HandleAetherFailedOnModelThread(std::move(error));
        });
      },
      [this](apptraverse::NetworkAvailability availability) {
        model_runtime_->Post([this, availability] {
          HandleNetworkObservationOnModelThread(availability);
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
  apptraverse::SaveDistilledRoot(*runtime_.application);  // runtime-save-ok: shutdown
  return static_cast<int>(msg.wParam);
}

}  // namespace chat::win32
