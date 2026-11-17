#if !defined(_WIN32)
int main() { return 0; }
#else
#include "window_local_environment_fixture.h"
int main() {
  using namespace apptraverse::test;
  NativeLocalWindowHarness h;
  auto const generation = h.model->window->Generation();
  h.Drag(10, 10); h.Pump(); auto a = h.Capture();
  h.Drag(20, 20); h.Pump(); auto b = h.Capture();
  h.Drag(10, 10); h.Pump(); auto final = h.Capture();
  for (auto const* publication : {&a, &b, &final}) { h.Apply(*publication); h.CheckNative(10, 10); }
  h.CheckPortableInitial(); LOCAL_CHECK(h.model->window->Generation() == generation);
  // A1 keeps native movement out of the portable semantic frame. Requesting
  // exactly that original target must still restore the actual native window.
  auto const journal_size = h.model->window->journal.size();
  h.model->window->RequestFrame(100, 100, 640, 480);
  LOCAL_CHECK(h.model->window->journal.size() == journal_size);
  h.Publish(); h.CheckNative(100, 100); h.CheckNativeSize(640, 480);
  h.Pump();
  h.model->window->RequestFrame(300, 200, 640, 480); h.Publish(); h.CheckNative(300, 200);
  h.Pump();
  h.model->window->presenter->locale->SubmitLanguageIndexFromUi(1);
  h.model->window->presenter->title->MaterializeLanguage(1); h.Publish(); h.CheckNative(300, 200);
  std::cout << "presenter_win_desktop_frame_publication_e2e_test OK\n";
}
#endif
