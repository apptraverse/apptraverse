#include "win_app.h"

#include <cassert>
#include <chrono>
#include <string>

#include "apptraverse/distill.h"
#include "apptraverse/graph_mirror.h"

#include "demo_commands.h"
#include "demo_ids.h"
#include "demo_log.h"
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
  auto* app = reinterpret_cast<WinApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (app != nullptr && msg == WM_APPTRAVERSE_PUBLISHED) {
    app->OnPublished(static_cast<std::uint32_t>(wparam),
                     reinterpret_cast<PublicationChannel<3>*>(lparam));
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

void WinApp::OnPublished(std::uint32_t root_id,
                         PublicationChannel<3>* channel) {
  ApplyPublication(root_id, channel);
}

void WinApp::ApplyPublication(std::uint32_t root_id,
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
  auto const paint_id = demo::ToObjId(demo::DemoObjId::PaintWindow);
  auto const layout_id = demo::ToObjId(demo::DemoObjId::LayoutWindow);
  if (presentation_) {
    if (applied.root_id == paint_id) {
      presentation_->PresentPaintWindow();
    } else if (applied.root_id == layout_id) {
      presentation_->PresentLayoutWindow();
    }
  }
  demo::DemoLog("ui apply root=" + std::to_string(applied.root_id) +
                " changed=" + std::to_string(applied.changed_obj_ids.size()));
}

void WinApp::RequestExit() {
  if (exiting_) {
    return;
  }
  exiting_ = true;
  if (model_runtime_) {
    model_runtime_->RequestStop();
  }
  if (presentation_) {
    presentation_->Destroy();
  }
  PostQuitMessage(0);
}

int WinApp::Run(std::filesystem::path const& state_dir) {
  EnsureDemoRegistration();
  EnsureWindowsPresenterRegistration();
  runtime_ = LoadDemoModel(state_dir);
  auto ui_root = CopyModelGraphToUiDomain(*runtime_.application, *runtime_.ui_domain,
                                          *runtime_.ui_storage);
  runtime_.ui_application = Application::ptr::MakeFromThis(
      static_cast<Application*>(ui_root.get()));
  ui_thread_ = GetCurrentThreadId();

  WNDCLASSW wc{};
  wc.lpfnWndProc = &DispatcherProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"AppTraverseModelUiDispatcher";
  RegisterClassW(&wc);
  dispatcher_ = CreateWindowExW(0, L"AppTraverseModelUiDispatcher", L"", 0, 0,
                                0, 0, 0, HWND_MESSAGE, nullptr,
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

  ui_mirror_ = std::make_unique<UiMirror>(*runtime_.ui_domain, *runtime_.ui_storage,
                                          notify);
  model_runtime_ =
      std::make_unique<ModelRuntime>(*runtime_.application, *ui_mirror_);
  model_runtime_->AddPresentationRoot(*runtime_.application->window_a);
  model_runtime_->AddPresentationRoot(*runtime_.application->window_b);

  presentation_ = LoadApplication<WinPresentationApplication>(
      *runtime_.ui_domain,
      ae::ObjId{demo::ToObjId(demo::DemoObjId::WinPresentationApplication)});
  presentation_->commands = [this](WindowBoundsCommand command) {
    model_runtime_->Post([app = &*runtime_.application,
                          command = std::move(command)] {
      CommitWindowBounds(*WindowById(*app, command.window_id), command);
    });
  };
  presentation_->on_close = [this] { RequestExit(); };
  presentation_->on_text_toolbar_activate = [this] {
    AdvanceToolbarTextCommand command;
    command.toolbar_id = demo::ToObjId(demo::DemoObjId::TextToolbar);
    model_runtime_->Post([app = &*runtime_.application, command] {
      auto* toolbar = TextToolbarById(*app, command.toolbar_id);
      assert(toolbar != nullptr);
      CommitAdvanceToolbarText(*toolbar);
    });
  };
  presentation_->OnLoad();

  model_runtime_->Start();

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  RequestExit();
  if (dispatcher_ != nullptr) {
    DestroyWindow(dispatcher_);
    dispatcher_ = nullptr;
  }
  if (model_runtime_) {
    model_runtime_->RequestStop();
    model_runtime_->Join();
  }
  SaveDistilledRoot(*runtime_.application);  // runtime-save-ok: shutdown
  return static_cast<int>(msg.wParam);
}

}  // namespace apptraverse
