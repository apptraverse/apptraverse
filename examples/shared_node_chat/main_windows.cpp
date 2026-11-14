#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif
#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "apptraverse/platform/windows/win32_fatal.h"
#include "apptraverse/platform/windows/win_ui_utf8.h"
#include "chat_controls.h"
#include "chat_ui_session.h"
#include <array>

#ifndef APPTRAVERSE_CHAT_TITLE
#  define APPTRAVERSE_CHAT_TITLE L"SharedNode Chat"
#endif

namespace apptraverse::chat {
namespace {
using ui::windows::Utf8ToWide;
using ui::windows::WideToUtf8;
constexpr UINT kViewChanged = WM_APP + 1;

void Close(HANDLE handle) {
  if (!CloseHandle(handle)) {
    DWORD const error = GetLastError();
    FatalWin32("CloseHandle chat profile mutex", error);
  }
}

void Place(HWND control, int x, int y, int width, int height) {
  if (!MoveWindow(control, x, y, width, height, TRUE)) {
    DWORD const error = GetLastError();
    FatalWin32("MoveWindow chat control", error);
  }
}

void SetText(HWND control, std::wstring const& value) {
  if (!SetWindowTextW(control, value.c_str())) {
    DWORD const error = GetLastError();
    FatalWin32("SetWindowTextW chat control", error);
  }
}

std::wstring Text(HWND control) {
  SetLastError(ERROR_SUCCESS);
  auto const size = GetWindowTextLengthW(control);
  if(size==0&&GetLastError()!=ERROR_SUCCESS)FatalWin32("GetWindowTextLengthW chat",GetLastError());
  std::wstring value(static_cast<std::size_t>(size) + 1, L'\0');
  auto const copied = GetWindowTextW(control, value.data(), size + 1);
  if(copied==0&&size!=0)FatalWin32("GetWindowTextW chat",GetLastError());
  value.resize(static_cast<std::size_t>(std::max(0, copied)));
  return value;
}

void SetChanged(HWND control, std::wstring const& value) {
  if (Text(control) != value) {
    SetText(control, value);
  }
}

struct Window : gui::Presentation {
  Window(std::filesystem::path profile,std::string name,bool host,std::wstring invited,bool load_only,int show)
    :session{std::move(profile),std::move(name),host,WideToUtf8(invited),[this]{
      if(!PostMessageW(hwnd,kViewChanged,0,0))FatalWin32("PostMessageW chat publication",GetLastError());
    },load_only},show_command{show}{}
  gui::Session session;
  HWND hwnd{}, input{}, invite{};
  std::array<HWND,static_cast<unsigned>(gui::Control::Count)> controls{};
  bool stopping{},applying{},window_initialized{};
  int show_command;
  bool shown{};
  std::vector<std::string> shown_people,shown_messages;
  HWND Handle(gui::Control c)const{return controls[static_cast<unsigned>(c)];}
  void Create(HWND parent){
    hwnd=parent;
    struct Definition { wchar_t const* type;DWORD style;int id; };
    Definition const definitions[]={
      {L"STATIC",0,0},{L"EDIT",WS_BORDER|ES_AUTOHSCROLL|ES_READONLY,kUidControl},
      {L"BUTTON",BS_PUSHBUTTON|WS_TABSTOP,kHostControl},
      {L"EDIT",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,kInviteControl},
      {L"BUTTON",BS_PUSHBUTTON|WS_TABSTOP,kJoinControl},{L"STATIC",0,0},
      {L"LISTBOX",WS_BORDER|WS_VSCROLL|LBS_NOINTEGRALHEIGHT,kParticipantsControl},
      {L"STATIC",0,0},
      {L"EDIT",WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,kMessagesControl},
      {L"EDIT",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,kInputControl},
      {L"BUTTON",BS_DEFPUSHBUTTON|WS_TABSTOP,kSendControl},{L"STATIC",SS_LEFT,kStatusControl}};
    for(unsigned i=0;i<controls.size();++i){
      auto const& d=definitions[i];
      auto control=CreateWindowExW((d.style&WS_BORDER)?WS_EX_CLIENTEDGE:0,d.type,L"",
        WS_CHILD|d.style,0,0,0,0,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(d.id)),
        GetModuleHandleW(nullptr),nullptr);
      if(!control)FatalWin32("CreateWindowExW chat control",GetLastError());
      EnableWindow(control,FALSE); // Native loading state before the first mirror.
      SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
      controls[i]=control;
    }
    input=Handle(gui::Control::Input);invite=Handle(gui::Control::Invite);
    SendMessageW(Handle(gui::Control::Messages),EM_SETLIMITTEXT,0,0);
    SendMessageW(input,EM_SETLIMITTEXT,8192,0);
    session.Start();
  }
  void PresentWindow(gui::ChatUi const& model)override{
    applying=true;
    if(!window_initialized){
      window_initialized=true;
      RECT bounds{0,0,model.restore_client_width(),model.restore_client_height()};
      if(!AdjustWindowRectEx(&bounds,WS_OVERLAPPEDWINDOW,FALSE,0))FatalWin32("AdjustWindowRectEx chat",GetLastError());
      auto flags=SWP_NOZORDER|(model.HasExplicitPosition()?0:SWP_NOMOVE);
      if(!SetWindowPos(hwnd,nullptr,model.x(),model.y(),bounds.right-bounds.left,bounds.bottom-bounds.top,flags))FatalWin32("SetWindowPos chat",GetLastError());
    }
    SetChanged(hwnd,Utf8ToWide(model.Title()));
    applying=false;
  }
  void RemoveWindow()override{ShowWindow(hwnd,SW_HIDE);}
  void Present(gui::Control kind,gui::ControlState const& state,gui::ChatUi const&)override{
    applying=true;
    auto control=Handle(kind);
    auto const& r=state.frame;Place(control,r.x,r.y,r.width,r.height);
    EnableWindow(control,state.enabled);ShowWindow(control,state.visible?SW_SHOW:SW_HIDE);
    if(kind==gui::Control::People){
      if(shown_people!=state.rows){
        SendMessageW(control,LB_RESETCONTENT,0,0);
        for(auto const& row:state.rows){auto value=Utf8ToWide(row);
          auto result=SendMessageW(control,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(value.c_str()));
          if(result==LB_ERR||result==LB_ERRSPACE)FatalWin32("LB_ADDSTRING chat participant",ERROR_NOT_ENOUGH_MEMORY);
        }
        shown_people=state.rows;
      }
    }else if(kind==gui::Control::Messages){
      if(shown_messages!=state.rows){std::string value;
        for(auto const& row:state.rows){if(!value.empty())value+="\r\n";value+=row;}
        SetChanged(control,Utf8ToWide(value));shown_messages=state.rows;
        SendMessageW(control,EM_SETSEL,static_cast<WPARAM>(-1),-1);SendMessageW(control,EM_SCROLLCARET,0,0);
      }
    }else SetChanged(control,Utf8ToWide(state.text));
    if(kind==gui::Control::Invite)SendMessageW(control,EM_SETREADONLY,state.read_only,0);
    applying=false;
  }
  void Remove(gui::Control control)override{ShowWindow(Handle(control),SW_HIDE);}
  void Refresh(){
    if(stopping||!session.ApplyPublication(*this))return;
    if(!shown){
      shown=true;ShowWindow(hwnd,show_command);UpdateWindow(hwnd);
      Resize();
    }
  }
  void Resize(){
    if(!window_initialized||applying||stopping||!shown||IsIconic(hwnd))return;
    RECT client{};if(!GetClientRect(hwnd,&client))FatalWin32("GetClientRect chat",GetLastError());
    RECT outer{};if(!GetWindowRect(hwnd,&outer))FatalWin32("GetWindowRect chat",GetLastError());
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if(!GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor))FatalWin32("GetMonitorInfoW chat",GetLastError());
    auto const& area=monitor.rcWork;
    session.Proxy().Invoke(gui::RootId(),&ui::UiDesktopWindow::SubmitScreenFromNative,
      static_cast<int>(area.left),static_cast<int>(area.top),static_cast<int>(area.right-area.left),static_cast<int>(area.bottom-area.top));
    session.Proxy().Invoke(gui::RootId(),&ui::UiDesktopWindow::SubmitFrameFromNative,
      static_cast<int>(outer.left),static_cast<int>(outer.top),static_cast<int>(client.right),static_cast<int>(client.bottom),!IsZoomed(hwnd));
  }
  void InputChanged(int id){
    if(applying||stopping)return;
    if(id==kInputControl)session.Proxy().Invoke(gui::RootId(),&gui::ChatUi::EditDraft,WideToUtf8(Text(input)));
    else if(id==kInviteControl)session.Proxy().Invoke(gui::RootId(),&gui::ChatUi::EditInvite,WideToUtf8(Text(invite)));
  }
  void CopyId(){
    auto const* mirror=session.Mirror();if(!mirror||!mirror->Get(gui::Control::Copy).enabled)return;
    auto value=Utf8ToWide(mirror->Get(gui::Control::Uid).text);bool copied=false;
    if(OpenClipboard(hwnd)){
      auto size=(value.size()+1)*sizeof(wchar_t);auto memory=GlobalAlloc(GMEM_MOVEABLE,size);
      auto* data=memory?GlobalLock(memory):nullptr;
      if(data){std::memcpy(data,value.c_str(),size);GlobalUnlock(memory);copied=EmptyClipboard()&&SetClipboardData(CF_UNICODETEXT,memory);}
      if(!copied&&memory)GlobalFree(memory);CloseClipboard();
    }
    session.Proxy().Invoke(gui::RootId(),&gui::ChatUi::ClipboardResult,copied);
  }
  void Join(){session.Proxy().Invoke(gui::RootId(),&gui::ChatUi::Join);}
  void Send(){session.Proxy().Invoke(gui::RootId(),&gui::ChatUi::Send);SetFocus(input);}
  void Close(){
    if(stopping)return;stopping=true;EnableWindow(hwnd,FALSE);session.Stop();
    if(!DestroyWindow(hwnd))FatalWin32("DestroyWindow chat",GetLastError());
  }
};

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam,
                            LPARAM lparam) {
  auto* window =
      reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto const* create = reinterpret_cast<CREATESTRUCTW const*>(lparam);
    window = static_cast<Window*>(create->lpCreateParams);
    SetLastError(ERROR_SUCCESS);
    auto const previous = SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                                            reinterpret_cast<LONG_PTR>(window));
    DWORD const error = GetLastError();
    if (previous == 0 && error != ERROR_SUCCESS) {
      FatalWin32("SetWindowLongPtrW chat window", error);
    }
  }
  if (!window) {
    return DefWindowProcW(hwnd, message, wparam, lparam);
  }
  switch (message) {
    case WM_CREATE:
      window->Create(hwnd);
      return 0;
    case WM_SIZE:
    case WM_MOVE:
      window->Resize();
      return 0;
    case WM_GETMINMAXINFO: {
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      if(auto const* model=window->session.Mirror()){
        RECT bounds{0,0,model->layout.minimum_width,model->layout.minimum_height};
        if(!AdjustWindowRectEx(&bounds,WS_OVERLAPPEDWINDOW,FALSE,0))FatalWin32("AdjustWindowRectEx chat minimum",GetLastError());
        info->ptMinTrackSize = {bounds.right-bounds.left,bounds.bottom-bounds.top};
      }
      return 0;
    }
    case kViewChanged:
      window->Refresh();
      return 0;
    case WM_COMMAND:
      if(HIWORD(wparam)==EN_CHANGE){window->InputChanged(LOWORD(wparam));return 0;}
      if(HIWORD(wparam)!=BN_CLICKED)break;
      switch (LOWORD(wparam)) {
        case kHostControl:
          window->CopyId();
          return 0;
        case kJoinControl:
          window->Join();
          return 0;
        case kSendControl:
          window->Send();
          return 0;
        default:
          break;
      }
      break;
    case WM_CLOSE:
      window->Close();
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

std::wstring ProfileMutex(std::filesystem::path const& profile) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (wchar_t c : profile.wstring()) {
    if (c >= L'A' && c <= L'Z') {
      c += L'a' - L'A';
    }
    hash ^= static_cast<std::uint64_t>(c);
    hash *= 1099511628211ULL;
  }
  return L"Local\\AppTraverseSharedNodeChat-" + std::to_wstring(hash);
}
}  // namespace
}  // namespace apptraverse::chat

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  using namespace apptraverse::chat;  // Native executable entry point.
  gui::EnsureUiRegistration();
  int argc = 0;
  auto* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) {
    DWORD const error = GetLastError();
    apptraverse::FatalWin32("CommandLineToArgvW chat", error);
  }
  bool is_host = false, role_seen = false, valid = true, load_only = false;
  std::filesystem::path distill_output;
  std::wstring name, profile_argument, host_id;
  for (int i = 1; i < argc && valid; ++i) {
    std::wstring const arg = argv[i];
    if(arg==L"--load-only"){load_only=true;continue;}
    if(arg==L"--distill-only"&&i+1<argc){distill_output=argv[++i];continue;}
    if (arg == L"--host" || arg == L"--client") {
      if (role_seen) { valid = false; break; }
      role_seen = true;
      is_host = arg == L"--host";
      if (!is_host && i + 1 < argc && std::wstring{argv[i + 1]}.find(L"--") != 0)
        host_id = argv[++i];
    } else if ((arg == L"--profile" || arg == L"--name") &&
               i + 1 < argc && std::wstring{argv[i + 1]}.find(L"--") != 0) {
      (arg == L"--profile" ? profile_argument : name) = argv[++i];
    } else valid = false;
  }
  LocalFree(argv);
  if(!distill_output.empty()) {
#ifdef APPTRAVERSE_CHAT_UI_PRODUCER
    return gui::ExportUiSupply(distill_output);
#else
    return 1;
#endif
  }
  if (!valid) {
    MessageBoxW(nullptr,
                L"Usage: shared_node_chat.exe [--host | --client [HOST-AETHER-ID]] "
                L"[--profile PATH] [--name NAME]", L"SharedNode Chat",
                MB_OK | MB_ICONERROR);
    return 1;
  }
  if (name.empty()) name = is_host ? L"Host" : L"Client";
  if (profile_argument.empty())
    profile_argument = is_host ? L"shared-chat-host-state" : L"shared-chat-client-state";
  std::error_code error;
  auto const profile =
      std::filesystem::absolute(profile_argument, error).lexically_normal();
  if (error) {
    MessageBoxW(nullptr, L"The profile path is invalid.", L"SharedNode Chat",
                MB_OK | MB_ICONERROR);
    return 1;
  }
  HANDLE const profile_lock =
      CreateMutexW(nullptr, FALSE, ProfileMutex(profile).c_str());
  DWORD const mutex_error = GetLastError();
  if (!profile_lock) {
    apptraverse::FatalWin32("CreateMutexW chat profile", mutex_error);
  }
  if (mutex_error == ERROR_ALREADY_EXISTS) {
    Close(profile_lock);
    MessageBoxW(nullptr,
                L"This profile is already open. Use a different --profile for "
                L"another participant.",
                L"SharedNode Chat", MB_OK | MB_ICONINFORMATION);
    return 1;
  }
  WNDCLASSEXW type{};
  type.cbSize = sizeof(type);
  type.hInstance = instance;
  type.lpfnWndProc = WindowProc;
  type.lpszClassName = kWindowClass;
  type.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  if (!RegisterClassExW(&type)) {
    DWORD const register_error = GetLastError();
    apptraverse::FatalWin32("RegisterClassExW chat window", register_error);
  }
  Window window{profile, apptraverse::ui::windows::WideToUtf8(name),
                is_host, std::move(host_id),load_only,show};
  auto hwnd = CreateWindowExW(0, kWindowClass, L"",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              0, 0, nullptr, nullptr, instance, &window);
  if (!hwnd) {
    DWORD const create_error = GetLastError();
    apptraverse::FatalWin32("CreateWindowExW chat window", create_error);
  }
  MSG message{};
  int result = 0;
  for (;;) {
    result = GetMessageW(&message, nullptr, 0, 0);
    if (result == -1) {
      DWORD const message_error = GetLastError();
      apptraverse::FatalWin32("GetMessageW chat window", message_error);
    }
    if (result == 0) {
      break;
    }
    if (message.hwnd == window.input && message.message == WM_KEYDOWN &&
        message.wParam == VK_RETURN) {
      window.Send();
      continue;
    }
    if (message.hwnd == window.invite && message.message == WM_KEYDOWN &&
        message.wParam == VK_RETURN) {
      window.Join();
      continue;
    }
    if (!IsDialogMessageW(hwnd, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
  window.session.Stop();
  Close(profile_lock);
  if (!UnregisterClassW(kWindowClass, instance)) {
    DWORD const unregister_error = GetLastError();
    apptraverse::FatalWin32("UnregisterClassW chat window", unregister_error);
  }
  return static_cast<int>(message.wParam);
}
