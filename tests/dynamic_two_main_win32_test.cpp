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

#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"

#include "dynamic_model.h"
#include "win_presenters.h"
#include "win32_fatal.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"     \
                << __LINE__ << '\n';                                        \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

void TestTwoMainPresenterInstancesShareClass() {
  RegisterDynamicWin32Classes();

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};

  auto window_a = MainWindow::ptr::Create(ae::CreateWith{domain}.with_id(501));
  auto presenter_a =
      Win32MainWindowPresenter::ptr::Create(ae::CreateWith{domain}.with_id(502));
  window_a->presenter = presenter_a;
  presenter_a->window = window_a;
  window_a->x = 40;
  window_a->y = 40;
  window_a->width = 320;
  window_a->height = 240;

  auto window_b = MainWindow::ptr::Create(ae::CreateWith{domain}.with_id(601));
  auto presenter_b =
      Win32MainWindowPresenter::ptr::Create(ae::CreateWith{domain}.with_id(602));
  window_b->presenter = presenter_b;
  presenter_b->window = window_b;
  window_b->x = 80;
  window_b->y = 80;
  window_b->width = 320;
  window_b->height = 240;

  InitializePresenters(*window_a);
  InitializePresenters(*window_b);

  auto* a = dynamic_cast<Win32MainWindowPresenter*>(&*presenter_a);
  auto* b = dynamic_cast<Win32MainWindowPresenter*>(&*presenter_b);
  CHECK(a != nullptr && b != nullptr);
  CHECK(a->hwnd != nullptr);
  CHECK(b->hwnd != nullptr);
  CHECK(a->hwnd != b->hwnd);
  CHECK(IsWindow(a->hwnd) != 0);
  CHECK(IsWindow(b->hwnd) != 0);

  UnloadPresenters(*window_a);
  CHECK(a->hwnd == nullptr);
  CHECK(IsWindow(b->hwnd) != 0);

  UnloadPresenters(*window_b);
  CHECK(b->hwnd == nullptr);

  UnregisterDynamicWin32Classes();
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureDynamicModelRegistration();
  apptraverse::test::TestTwoMainPresenterInstancesShareClass();
  std::cout << "dynamic_two_main_win32_test OK\n";
  return 0;
}
