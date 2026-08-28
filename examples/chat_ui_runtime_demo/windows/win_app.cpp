#include "win_app.h"

#include <cassert>
#include <string>

#include "apptraverse/distill.h"
#include "apptraverse/graph_mirror.h"

#include "chat_commands.h"
#include "chat_ids.h"
#include "chat_log.h"
#include "win_util.h"

namespace apptraverse {
namespace {

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
  if (app != nullptr && msg == WM_APPTRAVERSE_PUBLISHED) {
    app->OnPublished(static_cast<std::uint32_t>(wparam),
                     reinterpret_cast<PublicationChannel<3>*>(lparam));
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
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
  auto const chat_id = chat::ToObjId(chat::ChatObjId::ChatRoom);
  auto const aether_id = chat::ToObjId(chat::ChatObjId::LocalAetherIdentity);
  if (presentation_ && (applied.root_id == chat_id ||
                        applied.root_id == aether_id)) {
    if (applied.root_id == chat_id &&
        runtime_.ui_application->chat_room->clients.size() > 0) {
      auto const& ui_client = runtime_.ui_application->chat_room->clients[0];
      if (ui_client.is_valid()) {
        ui_client.Load();
        chat::ChatLog("UI_PRESENCE room_client0_online=" +
                      std::to_string(ui_client->online ? 1 : 0));
      }
    }
    presentation_->PresentChatWindow();
  }
  chat::ChatLog("ui apply root=" + std::to_string(applied.root_id) +
                " changed=" + std::to_string(applied.changed_obj_ids.size()));
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

int WinChatApp::Run(std::filesystem::path const& state_dir, ChatRole role) {
  EnsureChatRegistration();
  EnsureChatPresenterRegistration();
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

  presentation_ = LoadApplication<WinChatPresentationApplication>(
      *runtime_.ui_domain,
      ae::ObjId{chat::ToObjId(chat::ChatObjId::WinPresentationApplication)});
  presentation_->chat_window->application = runtime_.ui_application;
  presentation_->chat_window->room = runtime_.ui_application->chat_room;
  presentation_->chat_window->identity = runtime_.ui_application->local_aether;
  presentation_->on_close = [this] { RequestExit(); };
  presentation_->on_chat_send = [this](std::string text) {
    model_runtime_->Post([this, text = std::move(text)] {
      CommitLocalMessage(shared_, *runtime_.application->host_client,
                         std::move(text));
    });
  };
  presentation_->on_connect_host = [this](std::string host_uid) {
    model_runtime_->Post([this, host_uid = std::move(host_uid)] {
      ConnectToHostCommand(shared_, std::move(host_uid));
    });
  };
  presentation_->OnLoad();
  presentation_->PresentChatWindow();

  model_runtime_->Start();

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
        });
      },
      [this](bool online) {
        model_runtime_->Post([app = &*runtime_.application, online] {
          SetHostClientOnline(*app->host_client, online);
          chat::ChatLog("MODEL_PRESENCE host_online=" +
                        std::to_string(app->host_client->online ? 1 : 0));
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
