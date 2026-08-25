#include "win_app.h"

#include <cassert>
#include <string>

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
  (void)root_id;
  auto* buffer = channel->TakePublished();
  if (buffer == nullptr) {
    return;
  }
  auto applied = DeserializeUiSubgraphIntoExisting(buffer->sink, registry_,
                                                   runtime_.immutable_store);
  channel->ReleaseConsumer();

  CreateWindowsIfNeeded();

  auto const window_a_id = demo::ToObjId(demo::DemoObjId::WindowA);
  auto const window_b_id = demo::ToObjId(demo::DemoObjId::WindowB);
  if (applied.root_id == window_a_id) {
    window_a_.Present(*registry_.Must<RuntimeWindow>(window_a_id));
  } else if (applied.root_id == window_b_id) {
    window_b_.Present(*registry_.Must<RuntimeWindow>(window_b_id));
  }

  demo::DemoLog("ui apply root=" + std::to_string(applied.root_id) +
                " state=" + std::to_string(applied.changed_obj_ids.size()) +
                " reuse=" + std::to_string(applied.reused_obj_ids.size()) +
                " const=" + std::to_string(applied.const_ref_ids.size()));
}

void WinApp::CreateWindowsIfNeeded() {
  if (windows_created_) {
    return;
  }
  auto const window_a_id = demo::ToObjId(demo::DemoObjId::WindowA);
  auto const window_b_id = demo::ToObjId(demo::DemoObjId::WindowB);
  auto* ra = registry_.Find(window_a_id);
  auto* rb = registry_.Find(window_b_id);
  if (ra == nullptr || rb == nullptr) {
    return;
  }
  windows_created_ = true;
  auto commands = [this](ModelCommand command) {
    executor_->PostCommand(std::move(command));
  };
  window_a_.Create(*registry_.Must<RuntimeWindow>(window_a_id), L"Window A",
                   false, window_a_id, commands, nullptr, nullptr, 0, 0, 0);
  window_b_.Create(*registry_.Must<RuntimeWindow>(window_b_id), L"Window B",
                   true, window_b_id, commands, &runtime_.immutable_store,
                   &registry_, demo::ToObjId(demo::DemoObjId::TextToolbar),
                   demo::ToObjId(demo::DemoObjId::ColorToolbar),
                   demo::ToObjId(demo::DemoObjId::Chat));
}

int WinApp::Run(std::filesystem::path const& state_dir) {
  EnsureDemoRegistration();
  runtime_ = LoadDemo(state_dir);
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

  executor_ = std::make_unique<ModelExecutor>(*runtime_.graph.application,
                                              runtime_.immutable_store, notify);
  executor_->PumpOnce(std::chrono::steady_clock::now());
  executor_->Start();

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  executor_->RequestStop();
  executor_->Join();
  executor_->SaveShutdown();
  return static_cast<int>(msg.wParam);
}

}  // namespace apptraverse
