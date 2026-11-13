#include <chrono>
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
  apptraverse::chat::ChatSession session{argv[1], "Commit acceptance"};
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
  session.Stop();
  std::cout << "PASS rejected drafts and local model commit callback\n";
}
