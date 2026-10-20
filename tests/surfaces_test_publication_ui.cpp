#include "surfaces_test_publication_ui.h"

#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>

#include "surfaces_lifecycle.h"

namespace apptraverse::test {
namespace {

#define UI_CHECK(cond)                                                         \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"          \
                << __LINE__ << '\n';                                           \
      std::exit(1);                                                            \
    }                                                                          \
  } while (false)

Application::ptr LoadInitialUi(std::vector<std::uint8_t> const& bytes,
                               ae::Domain& ui_domain,
                               ae::IDomainStorage& ui_storage) {
  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  auto root = LoadInitialPublication(in, ui_domain, ui_storage);
  return Application::ptr::MakeFromThis(static_cast<Application*>(root.get()));
}

}  // namespace

void SurfacesUiPublicationConsumer::BindSession(
    SurfacesModelSession& session) {
  proxy = ModelObjectProxy{[&session](ModelObjectProxy::ModelWork work) {
    session.Post(std::move(work));
  }};
}

void SurfacesUiPublicationConsumer::LoadInitialFromSession(
    SurfacesModelSession& session) {
  std::vector<std::uint8_t> bytes;
  {
    std::unique_lock<std::mutex> lock{session.mu};
    UI_CHECK(session.cv.wait_for(lock, std::chrono::seconds{30}, [&] {
      return session.channel.has_unread_published();
    }));
    bytes = session.channel.TakePublishedCopy();
  }
  session.cv.notify_all();
  UI_CHECK(!bytes.empty());
  ui_app = LoadInitialUi(bytes, ui_domain, ui_storage);
  UI_CHECK(ui_app);
}

void SurfacesUiPublicationConsumer::ApplyStructuralBytes(
    std::vector<std::uint8_t> const& bytes) {
  UI_CHECK(ui_app);
  ApplySurfacesStructural(bytes, *ui_app, ui_storage, nullptr, &proxy);
  ++incremental_applies;
}

}  // namespace apptraverse::test
