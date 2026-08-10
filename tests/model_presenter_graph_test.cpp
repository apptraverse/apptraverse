#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/app.h"
#include "apptraverse/application_ids.h"
#include "apptraverse/chat.h"
#include "apptraverse/chat_presenter.h"
#include "apptraverse/node.h"
#include "apptraverse/window.h"
#include "apptraverse/window_presenter.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

static_assert(!std::is_base_of_v<apptraverse::Node, apptraverse::WindowPresenter>);
static_assert(!std::is_base_of_v<apptraverse::Node, apptraverse::ChatPresenter>);
static_assert(std::is_base_of_v<apptraverse::Node, apptraverse::Window>);
static_assert(std::is_base_of_v<apptraverse::Node, apptraverse::Chat>);

void DistillGraph(ae::Domain& domain) {
  auto app = App::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ApplicationObjId::Application)));
  auto window = Window::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ApplicationObjId::Window)));
  auto window_presenter = WindowPresenter::ptr::Create(ae::CreateWith{domain}
      .with_id(ToObjId(ApplicationObjId::WindowPresenter)));
  auto chat_base = Chat::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ApplicationObjId::ChatBase)));
  auto chat = Chat::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ApplicationObjId::Chat)));
  auto chat_presenter = ChatPresenter::ptr::Create(ae::CreateWith{domain}.with_id(
      ToObjId(ApplicationObjId::ChatPresenter)));

  app->window = window;

  window->presenter = window_presenter;
  window->chat = chat;

  window_presenter->window = window;
  window_presenter->chat_presenter = chat_presenter;

  chat->base = chat_base;
  chat->presenter = chat_presenter;

  chat_presenter->chat = chat;
  chat_presenter->window_presenter = window_presenter;

  app.Save();
}

void CheckLinks(App::ptr const& app) {
  CHECK(app.is_loaded());
  CHECK(app->window.is_valid());
  app->window.Load();
  CHECK(app->window.is_loaded());

  auto window = app->window;
  CHECK(window->presenter.is_valid());
  CHECK(window->chat.is_valid());
  CHECK(window->presenter.Load().get() != nullptr);
  CHECK(window->chat.Load().get() != nullptr);

  auto window_presenter = window->presenter;
  auto chat = window->chat;
  CHECK(window_presenter->window.id() == window.id());
  CHECK(window_presenter->chat_presenter.id() == chat->presenter.id());

  auto chat_presenter = chat->presenter;
  CHECK(chat_presenter->chat.id() == chat.id());
  CHECK(chat_presenter->window_presenter.id() == window_presenter.id());
  CHECK(window_presenter->chat_presenter.id() == chat_presenter.id());
}

void TestDistillSaveLoadCycles() {
  ae::RamDomainStorage storage;

  {
    ae::Domain domain{ae::Now(), storage};
    DistillGraph(domain);
  }

  {
    ae::Domain domain{ae::Now(), storage};
    auto app = App::ptr::Declare(ae::CreateWith{domain}.with_id(
        ToObjId(ApplicationObjId::Application)));
    app.Load();
    CheckLinks(app);
  }
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestDistillSaveLoadCycles();
  std::cout << "model_presenter_graph_test OK\n";
  return 0;
}
