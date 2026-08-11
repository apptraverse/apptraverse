#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>
#include <type_traits>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/object_macros.h"
#include "model/app.h"
#include "model/application_ids.h"
#include "model/chat.h"
#include "model/chat_entry.h"
#include "model/chat_events.h"
#include "model/chat_presenter.h"
#include "model/client.h"
#include "model/registration.h"
#include "apptraverse/node.h"
#include "apptraverse/node_for.h"
#include "model/window.h"
#include "model/window_changed_event.h"
#include "model/window_presenter.h"

#include "../examples/single_client_chat/common/chat_transcript.h"
#include "../examples/single_client_chat/common/graph_builder.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

class FakeChatPresenter : public ChatPresenter {
  APPTRAVERSE_OBJECT(FakeChatPresenter, ChatPresenter, 0)

 protected:
  FakeChatPresenter() = default;

 public:
  explicit FakeChatPresenter(ae::ObjProp prop) : ChatPresenter{prop} {}

  AE_OBJECT_REFLECT()
};

class FakeWindow : public NodeFor<FakeWindow, Window> {
  APPTRAVERSE_OBJECT(FakeWindow, Window, 0)

 protected:
  FakeWindow() = default;

 public:
  explicit FakeWindow(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(viewport_width), AE_MMBR(viewport_height),
                    AE_MMBR(density_dpi))

  std::int32_t viewport_width{0};
  std::int32_t viewport_height{0};
  std::int32_t density_dpi{96};

  void Apply(WindowChangedEvent const& event) override {
    viewport_width = event.available_right - event.available_left;
    viewport_height = event.available_bottom - event.available_top;
    density_dpi = event.density_dpi;
  }

  void InsertAtForTest(std::uint64_t timestamp_us, Event::ptr event) {
    InsertEvent(EventRecord{timestamp_us, std::move(event)});
  }
};

class FakeWindowPresenter : public WindowPresenter {
  APPTRAVERSE_OBJECT(FakeWindowPresenter, WindowPresenter, 0)

 protected:
  FakeWindowPresenter() = default;

 public:
  explicit FakeWindowPresenter(ae::ObjProp prop) : WindowPresenter{prop} {}

  AE_OBJECT_REFLECT()

  void CommitViewport(std::int32_t width, std::int32_t height,
                      std::int32_t density_dpi) {
    assert(window.is_valid());
    window.Load();
    auto event =
        WindowChangedEvent::ptr::Create(ae::CreateWith{*window.domain()});
    event->available_left = 0;
    event->available_top = 0;
    event->available_right = width;
    event->available_bottom = height;
    event->window_left = 0;
    event->window_top = 0;
    event->window_right = width;
    event->window_bottom = height;
    event->density_dpi = density_dpi;
    window->Commit(event);
  }
};

APPTRAVERSE_REGISTER(FakeChatPresenter);
APPTRAVERSE_REGISTER(FakeWindow);
APPTRAVERSE_REGISTER(FakeWindowPresenter);

WindowChangedEvent::ptr MakeWindowEvent(ae::Domain& domain, std::int32_t width,
                                        std::int32_t height,
                                        std::int32_t dpi = 96) {
  auto event = WindowChangedEvent::ptr::Create(ae::CreateWith{domain});
  event->available_left = 0;
  event->available_top = 0;
  event->available_right = width;
  event->available_bottom = height;
  event->window_left = 0;
  event->window_top = 0;
  event->window_right = width;
  event->window_bottom = height;
  event->density_dpi = dpi;
  return event;
}

void TestApplicationIds() {
  CHECK(ToObjId(ApplicationObjId::Application) >= 100000);
  CHECK(ToObjId(ApplicationObjId::WindowBase) >= 100000);
  CHECK(ToObjId(ApplicationObjId::ClientBase) >= 100000);
  CHECK(ToObjId(ApplicationObjId::WindowBase) !=
        ToObjId(ApplicationObjId::ClientBase));
}

void TestCommonGraphAndTranscript() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};

  auto graph = examples::BuildSingleClientChatGraph<FakeWindow, FakeWindowPresenter,
                                                    FakeChatPresenter>(domain);
  CHECK(graph.app.id().id() == ToObjId(ApplicationObjId::Application));
  CHECK(graph.alice->name == "Alice");
  CHECK(graph.alice->base.is_valid());
  CHECK(graph.alice->journal.empty());
  CHECK(graph.window->base.is_valid());
  CHECK(graph.window->journal.empty());
  CHECK(graph.chat->entries.size() == 1);
  CHECK(graph.chat_presenter->GetClassId() == FakeChatPresenter::kClassId);
  static_assert(std::is_base_of_v<Node, FakeWindow>);
  static_assert(std::is_base_of_v<Node, Window>);
  static_assert(!std::is_base_of_v<Node, FakeChatPresenter>);
  static_assert(!std::is_base_of_v<Node, FakeWindowPresenter>);

  auto const transcript = examples::FormatChatTranscriptUtf8(graph.chat);
  CHECK(transcript.find("* Alice joined") != std::string::npos);

  graph.chat_presenter->SubmitText("hello");
  auto const after = examples::FormatChatTranscriptUtf8(graph.chat);
  CHECK(after.find("Alice: hello") != std::string::npos);
}

void TestWindowNodeJournal() {
  ae::RamDomainStorage storage;
  ae::ObjId::Type const window_id = ToObjId(ApplicationObjId::Window);

  {
    ae::Domain domain{ae::Now(), storage};
    auto graph =
        examples::BuildSingleClientChatGraph<FakeWindow, FakeWindowPresenter,
                                             FakeChatPresenter>(domain);
    CHECK(graph.window->journal.empty());
    auto& fake = static_cast<FakeWindow&>(*graph.window);
    graph.window->Commit(MakeWindowEvent(domain, 1080, 1920, 420));
    CHECK(graph.window->journal.size() == 1);
    CHECK(fake.viewport_width == 1080);
    CHECK(fake.viewport_height == 1920);
    CHECK(fake.density_dpi == 420);
    graph.app.Save();
  }

  {
    ae::Domain domain{ae::Now(), storage};
    auto window =
        FakeWindow::ptr::Declare(ae::CreateWith{domain}.with_id(window_id));
    window.Load();
    CHECK(window.is_loaded());
    CHECK(window->journal.size() == 1);
    CHECK(window->viewport_width == 1080);
    CHECK(window->viewport_height == 1920);
    CHECK(window->density_dpi == 420);
    CHECK(window->base.is_valid());
  }
}

void TestRepeatedWindowEvents() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto graph = examples::BuildSingleClientChatGraph<FakeWindow, FakeWindowPresenter,
                                                    FakeChatPresenter>(domain);
  graph.window->Commit(MakeWindowEvent(domain, 800, 600));
  graph.window->Commit(MakeWindowEvent(domain, 800, 600));
  CHECK(graph.window->journal.size() == 2);
  auto& fake = static_cast<FakeWindow&>(*graph.window);
  CHECK(fake.viewport_width == 800);
  CHECK(fake.viewport_height == 600);
}

void TestWindowAndChatJournalsIndependent() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto graph = examples::BuildSingleClientChatGraph<FakeWindow, FakeWindowPresenter,
                                                    FakeChatPresenter>(domain);
  auto const chat_before = graph.chat->journal.size();
  graph.window->Commit(MakeWindowEvent(domain, 640, 480));
  CHECK(graph.chat->journal.size() == chat_before);
  auto const window_before = graph.window->journal.size();
  graph.chat_presenter->SubmitText("ping");
  CHECK(graph.window->journal.size() == window_before);
  CHECK(graph.chat->journal.size() == chat_before + 1);
}

void TestClientBaseReload() {
  ae::RamDomainStorage storage;
  ae::ObjId::Type const alice_id = ToObjId(ApplicationObjId::Alice);
  {
    ae::Domain domain{ae::Now(), storage};
    auto graph =
        examples::BuildSingleClientChatGraph<FakeWindow, FakeWindowPresenter,
                                             FakeChatPresenter>(domain);
    CHECK(graph.alice->base.is_valid());
    CHECK(graph.alice->journal.empty());
    graph.app.Save();
  }
  {
    ae::Domain domain{ae::Now(), storage};
    auto alice =
        Client::ptr::Declare(ae::CreateWith{domain}.with_id(alice_id));
    alice.Load();
    CHECK(alice.is_loaded());
    CHECK(alice->name == "Alice");
    CHECK(alice->base.is_valid());
    CHECK(alice->journal.empty());
  }
}

void TestWindowRebuildDoesNotReplaceChat() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto graph = examples::BuildSingleClientChatGraph<FakeWindow, FakeWindowPresenter,
                                                    FakeChatPresenter>(domain);
  graph.chat_presenter->SubmitText("kept");
  auto const chat_journal = graph.chat->journal.size();
  auto const chat_entries = graph.chat->entries.size();
  auto const presenter_id = graph.window->presenter.id();
  auto const chat_id = graph.window->chat.id();

  graph.window->Commit(MakeWindowEvent(domain, 100, 200));
  graph.window->Commit(MakeWindowEvent(domain, 300, 400));
  auto early = MakeWindowEvent(domain, 50, 60);
  auto& fake = static_cast<FakeWindow&>(*graph.window);
  fake.InsertAtForTest(graph.window->journal[0].timestamp_us - 1, early);

  CHECK(graph.window->journal.size() == 3);
  CHECK(fake.viewport_width == 300);
  CHECK(fake.viewport_height == 400);
  CHECK(graph.chat->journal.size() == chat_journal);
  CHECK(graph.chat->entries.size() == chat_entries);
  CHECK(graph.window->presenter.id() == presenter_id);
  CHECK(graph.window->chat.id() == chat_id);
}

void TestPresenterCommitPath() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto graph = examples::BuildSingleClientChatGraph<FakeWindow, FakeWindowPresenter,
                                                    FakeChatPresenter>(domain);
  auto& win_presenter =
      static_cast<FakeWindowPresenter&>(*graph.window_presenter);
  win_presenter.CommitViewport(1200, 800, 240);
  CHECK(graph.window->journal.size() == 1);
  auto& fake = static_cast<FakeWindow&>(*graph.window);
  CHECK(fake.viewport_width == 1200);
  CHECK(fake.viewport_height == 800);

  graph.chat_presenter->SubmitText("from_presenter");
  CHECK(graph.chat->journal.size() == 2);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSingleClientChatRegistration();
  apptraverse::test::TestApplicationIds();
  apptraverse::test::TestCommonGraphAndTranscript();
  apptraverse::test::TestWindowNodeJournal();
  apptraverse::test::TestRepeatedWindowEvents();
  apptraverse::test::TestWindowAndChatJournalsIndependent();
  apptraverse::test::TestClientBaseReload();
  apptraverse::test::TestWindowRebuildDoesNotReplaceChat();
  apptraverse::test::TestPresenterCommitPath();
  std::cout << "single_client_chat_test OK\n";
  return 0;
}
