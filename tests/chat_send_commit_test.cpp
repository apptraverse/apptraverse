#include <chrono>
#include <atomic>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <thread>
#include "apptraverse/chat_session.h"
#include "apptraverse/noninteractive_crt.h"

// Explicit cloud acceptance, never registered in ordinary CTest. Native UIs
// must retain a draft when the model rejects it and clear only a committed one.
int main(int argc, char** argv) {
  if (argc != 2 || std::filesystem::exists(argv[1])) return 2;
  apptraverse::EnableNoninteractiveCrt();
  std::atomic<int> flushes{0};
  apptraverse::chat::ChatSession session{argv[1], "Commit acceptance", {}, {},
                                        [&] { ++flushes; return true; }};
  session.Start();
  auto wait = [](auto&& predicate) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{90};
    while (std::chrono::steady_clock::now() < deadline) {
      if (predicate()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return false;
  };
  if (!wait([&] { return session.View().ready; })) return 3;
  auto send = [&](std::string text, bool expected) {
    auto result = std::make_shared<std::promise<bool>>();
    auto future = result->get_future();
    session.Send(std::move(text), [result](bool committed) { result->set_value(committed); });
    if (future.wait_for(std::chrono::seconds{10}) != std::future_status::ready) {
      session.Stop();  // callback owner must outlive the worker even on timeout
      return false;
    }
    return future.get() == expected;
  };
  if (!send("not admitted", false)) return 4;
  session.CreateRoom();
  if (!wait([&] { return session.View().in_room; })) return 5;
  if (!send(" \t", false) || !send(std::string(8193, 'x'), false)) return 6;
  if (!session.View().messages.empty()) return 7;
  if (!send("committed draft", true)) return 8;
  if (!wait([&] { return session.View().messages.size() == 1; })) return 9;
  auto const committed = session.View();
  if (committed.message_records.size() != 1 || flushes.load() == 0) return 10;
  auto const record = committed.message_records.front();
  if (record.id.empty() || record.author_uid != committed.local_uid ||
      record.text != "committed draft" || record.author_name != "Commit acceptance") return 11;
  session.Stop();
  if (!send("after stop", false)) return 12;
  session.Start();
  if (!wait([&] { return session.View().ready && session.View().in_room; })) return 13;
  if (session.View().message_records != committed.message_records) return 14;
  session.Stop();

  auto failed = std::make_shared<std::promise<bool>>();
  auto rejected = failed->get_future();
  apptraverse::chat::ChatSession failure{std::string{argv[1]} + "-failed", "Failure",
                                        {}, [] { return false; }};
  failure.Start();
  failure.Send("cannot be accepted", [failed](bool accepted) { failed->set_value(accepted); });
  if (rejected.wait_for(std::chrono::seconds{3}) != std::future_status::ready ||
      rejected.get()) return 15;
  failure.Stop();
  std::cout << "PASS rejected drafts, local commit, durable structured records/cold restore, shutdown and startup-failure completions\n";
}
