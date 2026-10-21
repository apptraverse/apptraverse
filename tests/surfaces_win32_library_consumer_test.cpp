#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <cstdlib>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/platform/windows/surfaces_win_presenters.h"
#include "apptraverse/surfaces/surfaces_presentation.h"
#include "apptraverse/platform/windows/surfaces_win_registration.h"
#include "apptraverse/platform/windows/win32_fatal.h"
#include "apptraverse/surfaces/surfaces_localization.h"
#include "apptraverse/surfaces/surfaces_model.h"
#include "apptraverse/surfaces/surfaces_registration.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << '\n';                           \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

LRESULT CALLBACK NotifyWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void PumpPendingWin32Messages() {
  MSG msg{};
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

Application::ptr BuildMinimalConsumerGraph(ae::Domain& domain) {
  auto application = Application::ptr::Create(ae::CreateWith{domain});
  auto surfaces = Surfaces::ptr::Create(ae::CreateWith{domain});
  auto surface = Surface::ptr::Create(ae::CreateWith{domain});
  surface->number = 1;
  AssignInitialDesktopBounds(*surface);
  surface->surfaces = surfaces;
  surfaces->surfaces.push_back(surface);
  application->surfaces = surfaces;
  InstallSurfacesDemoLocalization(*application, domain);
  AttachPlatformSurfacePresenters(*surface, *application, domain);
  return application;
}

}  // namespace

void RunSurfacesWin32LibraryConsumerTest() {
  EnsureObjectRegistration();
  EnsureSurfacesRegistration();
  EnsureWin32SurfacesPresenterRegistration();

  HINSTANCE const instance = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.lpfnWndProc = NotifyWndProc;
  wc.hInstance = instance;
  wc.lpszClassName = L"AppTraverseSurfacesLibraryConsumerNotify";
  CHECK(RegisterClassW(&wc) != 0);

  HWND const notify = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, nullptr, instance, nullptr);
  CHECK(notify != nullptr);

  RegisterSurfacesWin32Classes();

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) {
    work(domain);
  }};
  auto application = BuildMinimalConsumerGraph(domain);

  InitializePresenters(*application, notify, &proxy);
  PumpPendingWin32Messages();
  CHECK(application->surfaces->surfaces.size() == 1);
  if (Win32SurfaceWindowPresenter::ptr window =
          ResolveWin32WindowPresenter(*application->surfaces->surfaces[0])) {
    CHECK(window->hwnd != nullptr);
    CHECK(IsWindow(window->hwnd));
  } else {
    CHECK(false);
  }

  UnloadPresenters(*application);
  PumpPendingWin32Messages();
  UnregisterSurfacesWin32Classes();
  CHECK(DestroyWindow(notify) != 0);
  CHECK(UnregisterClassW(wc.lpszClassName, instance) != 0);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunSurfacesWin32LibraryConsumerTest();
  std::cout << "surfaces_win32_library_consumer_test OK\n";
  return 0;
}
