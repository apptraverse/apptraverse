#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include "session.h"
#include <algorithm>
namespace update_lab {
namespace {
std::wstring Wide(std::string const& text) {
  int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring out(n, 0); MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n); return out;
}
std::string Utf8(std::wstring const& text) {
  int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  std::string out(n, 0); WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr); return out;
}
enum { LanguageList = 101, ReleaseList, NoteEdit, SaveNote, Add, SlowControl, Bad, ExportUser, ImportUser };
class Window {
 public:
  Options const& options;
  apptraverse::ApplicationModelRuntime runtime;
  apptraverse::ModelObjectProxy proxy{[this](auto work) { runtime.Post(std::move(work)); }};
  std::unique_ptr<apptraverse::ApplicationUiSession> ui;
  std::thread model;
  int result{1};
  HWND window{}, greeting{}, language_label{}, release_label{}, note_label{}, note{}, languages{}, releases{}, clones{}, progress{}, status{}, slow{}, save{}, add{}, bad{}, export_button{}, import_button{};
  HFONT font{};
  bool initialized{false}, rendering{false}, smoke_started{false};
  unsigned dpi{96};
  explicit Window(Options const& value) : options{value} {}
  int Scale(int value) const { return MulDiv(value, static_cast<int>(dpi), 96); }
  void Send(Command c) { proxy.Invoke(kViewId, &LabView::Submit, std::move(c)); }
  HWND Control(wchar_t const* type, unsigned style, int id) {
    HWND c = CreateWindowExW(type == WC_LISTBOXW || type == WC_EDITW ? WS_EX_CLIENTEDGE : 0,
        type, L"", WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    if (!c) Fail("native control creation failed");
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE); return c;
  }
  void Text(HWND control, std::string const& text) { auto s = Wide(text); SetWindowTextW(control, s.c_str()); }
  void Layout() {
    RECT rect; GetClientRect(window, &rect);
    int w = MulDiv(rect.right, 96, static_cast<int>(dpi)), h = MulDiv(rect.bottom, 96, static_cast<int>(dpi));
    auto pos = [&](HWND c, int x, int y, int width, int height) { MoveWindow(c, Scale(x), Scale(y), Scale(std::max(1, width)), Scale(std::max(1, height)), TRUE); };
    pos(greeting, 20, 16, w-40, 44);
    pos(language_label, 20, 68, 200, 24); pos(release_label, w/2+10, 68, 200, 24);
    pos(languages, 20, 96, w/2-30, 90); pos(releases, w/2+10, 96, w/2-30, 90);
    pos(note_label, 20, 202, w-40, 24); pos(note, 20, 230, w-200, 30); pos(save, w-170, 230, 150, 30);
    pos(clones, 20, 276, w-200, std::max(60, h-450)); pos(add, w-170, 276, 150, 32);
    pos(slow, 20, h-155, w-40, 24); pos(progress, 20, h-122, w-40, 20);
    pos(status, 20, h-96, w-40, 38);
    pos(export_button, 20, h-48, 160, 30); pos(import_button, 190, h-48, 160, 30); pos(bad, 360, h-48, w-380, 30);
  }
  void Create() {
    HDC dc = GetDC(nullptr); dpi = static_cast<unsigned>(GetDeviceCaps(dc, LOGPIXELSX)); ReleaseDC(nullptr, dc);
    font = CreateFontW(-Scale(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    greeting = Control(WC_STATICW, 0, 0); language_label = Control(WC_STATICW, 0, 0); release_label = Control(WC_STATICW, 0, 0);
    languages = Control(WC_LISTBOXW, LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP, LanguageList);
    releases = Control(WC_LISTBOXW, LBS_NOTIFY | WS_TABSTOP, ReleaseList);
    for (auto text : {L"R0 — initial", L"R1 — translation", L"R2 — new language", L"R3 — S1 prefab / E1"}) SendMessageW(releases, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    note_label = Control(WC_STATICW, 0, 0); note = Control(WC_EDITW, ES_AUTOHSCROLL | WS_TABSTOP, NoteEdit);
    save = Control(WC_BUTTONW, BS_PUSHBUTTON | WS_TABSTOP, SaveNote);
    clones = Control(WC_LISTBOXW, WS_VSCROLL, 0); add = Control(WC_BUTTONW, BS_PUSHBUTTON | WS_TABSTOP, Add);
    slow = Control(WC_BUTTONW, BS_AUTOCHECKBOX | WS_TABSTOP, SlowControl);
    progress = Control(PROGRESS_CLASSW, 0, 0); status = Control(WC_STATICW, 0, 0);
    bad = Control(WC_BUTTONW, BS_PUSHBUTTON | WS_TABSTOP, Bad);
    export_button = Control(WC_BUTTONW, BS_PUSHBUTTON | WS_TABSTOP, ExportUser);
    import_button = Control(WC_BUTTONW, BS_PUSHBUTTON | WS_TABSTOP, ImportUser);
    Layout();
  }
  void Render() {
    auto bytes = runtime.TakePublishedCopyForUiApply();
    if (bytes.empty()) return;
    if (!ui) ui = std::make_unique<apptraverse::ApplicationUiSession>(bytes, nullptr, proxy, nullptr, false);
    else ui->ApplyStructural(bytes);
    runtime.NotifyPublicationConsumed();
    auto const& view = static_cast<LabView&>(ui->root()); auto const& d = view.data;
    rendering = true;
    if (!initialized) {
      initialized = true;
      SetWindowPos(window, nullptr, Scale(d.x), Scale(d.y), Scale(d.width), Scale(d.height), SWP_NOZORDER);
      Send({"initial", "", 0, 0, 0, 0});
    }
    Text(window, d.text[Title] + " · E" + std::to_string(d.executable) + " / R" + std::to_string(d.release));
    Text(greeting, d.ready ? d.text[Greeting] : d.text[Downloading]);
    Text(language_label, d.text[Language]); Text(release_label, d.text[Version]); Text(note_label, d.text[Note]);
    if (GetFocus() != note) Text(note, d.note);
    Text(save, d.text[ApplyNote]); Text(add, d.text[AddClone]); Text(slow, d.text[Slow]); Text(bad, d.text[Broken]);
    Text(export_button, d.text[Export]); Text(import_button, d.text[Import]);
    SendMessageW(slow, BM_SETCHECK, d.rate ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(languages, LB_RESETCONTENT, 0, 0);
    for (std::size_t n = 0; n < d.languages.size(); ++n) {
      auto const& code = d.languages[n]; auto name = Wide(code == "en" ? "English" : code == "ru" ? "Русский" : "Deutsch");
      SendMessageW(languages, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
      if (code == d.language) SendMessageW(languages, LB_SETCURSEL, n, 0);
    }
    SendMessageW(releases, LB_SETCURSEL, d.release, 0);
    SendMessageW(clones, LB_RESETCONTENT, 0, 0);
    for (auto const& c : d.clones) { auto text = Wide(d.text[Clone] + " " + c); SendMessageW(clones, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str())); }
    std::uint64_t done = 0, total = 0;
    auto const& download = view.download->state;
    for (auto const& f : download.files) if (std::find(download.requested.begin(), download.requested.end(), f.id) != download.requested.end()) { done += f.received; total += f.size; }
    SendMessageW(progress, PBM_SETRANGE32, 0, 1000); SendMessageW(progress, PBM_SETPOS, total ? done * 1000 / total : 0, 0);
    std::string label = download.phase == "error" ? d.text[Failure] : download.phase == "activated" || download.phase == "idle" ? d.text[Ready] : d.text[Downloading];
    Text(status, label + " · " + std::to_string(done) + " / " + std::to_string(total) + " B · " + d.device + "\n" + d.detail);
    rendering = false;
    AcknowledgeHealth(options, view);
    if (!options.snapshot_file.empty()) { auto json = InspectJson(view); if (!AtomicWrite(options.snapshot_file, Bytes{json.begin(), json.end()})) Fail("snapshot write failed"); }
    if (options.native_smoke && !smoke_started && d.commands_done) {
      smoke_started = true; SetTimer(window, 2, 100, nullptr);
    }
    if (options.native_smoke && smoke_started && download.phase == "activated" &&
        d.language == "ru" && d.clones.size() == 3 && d.rate == 65536) {
      wchar_t actual[512]{}; GetWindowTextW(greeting, actual, 512);
      if (std::wstring{actual} != Wide(d.text[Greeting]) || SendMessageW(languages, LB_GETCURSEL, 0, 0) != 1)
        Fail("native language presentation differs from the model publication");
      if (!AtomicWrite(options.profile / "native-smoke.ok", Bytes{'1'})) Fail("native acceptance marker write failed");
      PostMessageW(window, WM_CLOSE, 0, 0);
    }
    if (d.restarting) PostMessageW(window, WM_CLOSE, 0, 0);
  }
  void FileDialog(bool save_file) {
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof dialog; dialog.hwndOwner = window;
    dialog.lpstrFilter = L"AppTraverse state (*.ats)\0*.ats\0\0"; dialog.lpstrFile = path; dialog.nMaxFile = 32768;
    dialog.lpstrDefExt = L"ats"; dialog.Flags = OFN_NOCHANGEDIR | (save_file ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (save_file ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))
      Send({save_file ? "export" : "import", Utf8(path), 0, 0, 0, 0});
  }
  void CommandMessage(WPARAM parameter) {
    if (rendering || !ui) return;
    auto const& d = static_cast<LabView&>(ui->root()).data;
    unsigned id = LOWORD(parameter), event = HIWORD(parameter);
    if (id == LanguageList && event == LBN_SELCHANGE) {
      auto n = SendMessageW(languages, LB_GETCURSEL, 0, 0);
      if (n >= 0 && static_cast<std::size_t>(n) < d.languages.size()) Send({"language", d.languages[n], 0, 0, 0, 0});
      for (std::size_t i=0;i<d.languages.size();++i) if(d.languages[i]==d.language) SendMessageW(languages,LB_SETCURSEL,i,0);
    } else if (id == ReleaseList && event == LBN_SELCHANGE) Send({"release", "", static_cast<int>(SendMessageW(releases, LB_GETCURSEL, 0, 0)), 0, 0, 0});
    else if (id == SaveNote) { int n = GetWindowTextLengthW(note); std::wstring value(n + 1, 0); GetWindowTextW(note, value.data(), n + 1); value.resize(n); Send({"note", Utf8(value), 0, 0, 0, 0}); }
    else if (id == Add) Send({"clone", "", 0, 0, 0, 0});
    else if (id == SlowControl) Send({"rate", "", SendMessageW(slow, BM_GETCHECK, 0, 0) == BST_CHECKED ? 65536 : 0, 0, 0, 0});
    else if (id == Bad) Send({"broken", "", 0, 0, 0, 0});
    else if (id == ExportUser) FileDialog(true);
    else if (id == ImportUser) FileDialog(false);
  }
  static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    auto self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
      self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
      self->window = hwnd; SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, msg, w, l);
    if (msg == WM_CREATE) { self->Create(); return 0; }
    if (msg == WM_SIZE) { self->Layout(); return 0; }
    if (msg == WM_GETMINMAXINFO) { auto m = reinterpret_cast<MINMAXINFO*>(l); m->ptMinTrackSize = {self->Scale(650), self->Scale(570)}; return 0; }
    if (msg == WM_APP + 1) { self->Render(); return 0; }
    if (msg == WM_COMMAND) { self->CommandMessage(w); return 0; }
    if (msg == WM_TIMER) {
      KillTimer(hwnd, w);
      if (w == 2) {
        SendMessageW(self->slow, BM_CLICK, 0, 0);
        SendMessageW(self->languages, LB_SETCURSEL, 1, 0);
        SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(LanguageList, LBN_SELCHANGE), reinterpret_cast<LPARAM>(self->languages));
        SendMessageW(self->add, BM_CLICK, 0, 0);
      } else PostMessageW(hwnd, WM_CLOSE, 0, 0);
      return 0;
    }
    if (msg == WM_CLOSE) {
      if (self->ui) { RECT r; GetWindowRect(hwnd, &r); self->Send({"frame", "", MulDiv(r.left,96,self->dpi), MulDiv(r.top,96,self->dpi), MulDiv(r.right-r.left,96,self->dpi), MulDiv(r.bottom-r.top,96,self->dpi)}); }
      self->runtime.RequestStop(); DestroyWindow(hwnd); return 0;
    }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, msg, w, l);
  }
};
}
int RunNative(Options const& options) {
  INITCOMMONCONTROLSEX controls{sizeof controls, ICC_PROGRESS_CLASS}; InitCommonControlsEx(&controls);
  Window app{options};
  WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"AppTraverseUpdateLab";
  cls.lpfnWndProc = Window::Proc; cls.hCursor = LoadCursor(nullptr, IDC_ARROW); cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
  if (!RegisterClassW(&cls)) Fail("native window class registration failed");
  HWND window = CreateWindowW(cls.lpszClassName, L"UpdateLab", WS_OVERLAPPEDWINDOW,
      160, 120, 820, 720, nullptr, nullptr, cls.hInstance, &app);
  if (!window) Fail("native window creation failed");
  app.model = std::thread{[&] { Session session{options, app.runtime}; app.result = session.Run([&](auto) { PostMessageW(window, WM_APP+1,0,0); }); }};
  ShowWindow(window, SW_SHOW); UpdateWindow(window);
  if (options.auto_close_ms) SetTimer(window, 1, options.auto_close_ms, nullptr);
  MSG message;
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
  }
  app.runtime.RequestStop(); app.runtime.AbandonUnconsumedPublicationsForTeardown(); app.model.join();
  if (app.font) DeleteObject(app.font);
  return app.result;
}
}
