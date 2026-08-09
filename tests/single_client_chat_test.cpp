#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/app.h"
#include "apptraverse/chat.h"
#include "apptraverse/chat_entry.h"
#include "apptraverse/chat_events.h"
#include "apptraverse/chat_presenter.h"
#include "apptraverse/client.h"
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

class FakeChatPresenter : public ChatPresenter {
  AE_OBJECT(FakeChatPresenter, ChatPresenter, 0)

 protected:
  FakeChatPresenter() = default;

 public:
  explicit FakeChatPresenter(ae::ObjProp prop) : ChatPresenter{prop} {}

  AE_OBJECT_REFLECT()
};

void WaitForNextTimestamp(Chat::ptr const& chat) {
  if (chat->journal.empty()) {
    return;
  }
  auto const last = chat->journal.back().timestamp_us;
  while (SystemUtcMicros() <= last) {
    std::this_thread::sleep_for(std::chrono::microseconds(1));
  }
}

void TestSingleClientChat() {
  ae::RamDomainStorage storage;
  ae::ObjId::Type const app_id = 200;

  {
    ae::Domain domain{ae::Now(), storage};

    auto app = App::ptr::Create(ae::CreateWith{domain}.with_id(app_id));
    auto window =
        Window::ptr::Create(ae::CreateWith{domain}.with_id(app_id + 1));
    auto window_presenter = WindowPresenter::ptr::Create(
        ae::CreateWith{domain}.with_id(app_id + 2));
    auto chat_base =
        Chat::ptr::Create(ae::CreateWith{domain}.with_id(app_id + 3));
    auto chat = Chat::ptr::Create(ae::CreateWith{domain}.with_id(app_id + 4));
    auto chat_presenter = FakeChatPresenter::ptr::Create(
        ae::CreateWith{domain}.with_id(app_id + 5));
    auto alice =
        Client::ptr::Create(ae::CreateWith{domain}.with_id(app_id + 6));

    alice->name = "Alice";

    app->window = window;
    window->presenter = window_presenter;
    window->chat = chat;
    window_presenter->window = window;
    window_presenter->chat_presenter = chat_presenter;

    chat->base = chat_base;
    chat->presenter = chat_presenter;
    chat->CaptureBaseStateForDistill();

    chat_presenter->chat = chat;
    chat_presenter->window_presenter = window_presenter;

    auto join = JoinClientEvent::ptr::Create(
        ae::CreateWith{domain}.with_id(app_id + 7));
    join->client = alice;
    chat->Commit(join);

    CHECK(chat->journal.size() == 1);
    CHECK(chat->entries.size() == 1);
    CHECK(chat->entries[0]->GetClassId() == JoinClientEntry::kClassId);
    CHECK(static_cast<JoinClientEntry&>(*chat->entries[0]).client.id() ==
          alice.id());

    WaitForNextTimestamp(chat);
    chat_presenter->SubmitText("hello");

    CHECK(chat->journal.size() == 2);
    CHECK(chat->entries.size() == 2);
    CHECK(chat->entries[1]->GetClassId() == MessageEntry::kClassId);
    auto& message = static_cast<MessageEntry&>(*chat->entries[1]);
    CHECK(message.text == "hello");
    CHECK(message.author.id() == alice.id());
    CHECK(static_cast<JoinClientEntry&>(*chat->entries[0]).client.id() ==
          message.author.id());

    auto const* join_event = static_cast<JoinClientEvent const*>(
        chat->journal[0].event.Load().get());
    auto const* add_event = static_cast<AddMessageEvent const*>(
        chat->journal[1].event.Load().get());
    CHECK(join_event->client.id() == alice.id());
    CHECK(add_event->author.id() == alice.id());

    app.Save();
  }

  {
    ae::Domain domain{ae::Now(), storage};
    auto app = App::ptr::Declare(ae::CreateWith{domain}.with_id(app_id));
    app.Load();
    CHECK(app.is_loaded());

    auto window = app->window;
    window.Load();
    auto chat = window->chat;
    chat.Load();
    CHECK(chat->journal.size() == 2);
    CHECK(chat->entries.size() == 2);

    auto& join_entry = static_cast<JoinClientEntry&>(*chat->entries[0]);
    join_entry.client.Load();
    CHECK(join_entry.client->name == "Alice");

    auto& message = static_cast<MessageEntry&>(*chat->entries[1]);
    message.author.Load();
    CHECK(message.text == "hello");
    CHECK(message.author.id() == join_entry.client.id());
    CHECK(message.author->name == "Alice");

    auto presenter = chat->presenter;
    presenter.Load();
    CHECK(presenter->GetClassId() == FakeChatPresenter::kClassId);
  }
}

}  // namespace apptraverse::test

int main() {
  apptraverse::test::TestSingleClientChat();
  std::cout << "single_client_chat_test OK\n";
  return 0;
}
