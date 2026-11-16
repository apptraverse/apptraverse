#include <condition_variable>
#include <deque>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "apptraverse/chat_session.h"

// Native consumer of the same ChatSession used by the GUI ports. It creates no
// UI graph, presenter or window, and uses the ordinary embedded transport supply.
int main(int argc, char** argv) {
  if (argc != 3 || argv[1][0] == '\0' || argv[2][0] == '\0') {
    std::cerr << "Usage: shared_node_chat_headless PROFILE NAME\n";
    return 2;
  }
  std::mutex mutex;
  std::condition_variable changed;
  std::deque<std::string> commands;
  bool published = false;
  apptraverse::chat::ChatSession session{
      std::filesystem::absolute(argv[1]), argv[2], [&] {
        { std::lock_guard lock{mutex}; published = true; }
        changed.notify_one();
      }};
  session.Start();
  std::thread input{[&] {
    for (std::string line; std::getline(std::cin, line);) {
      bool const stop = line == "stop";
      { std::lock_guard lock{mutex}; commands.push_back(std::move(line)); }
      changed.notify_one();
      if (stop) return;
    }
    { std::lock_guard lock{mutex}; commands.push_back("stop"); }
    changed.notify_one();
  }};
  std::string last_uid;
  std::size_t shown = 0;
  bool running = true;
  std::deque<std::string> deferred;
  while (running) {
    std::deque<std::string> pending;
    {
      std::unique_lock lock{mutex};
      changed.wait(lock, [&] { return published || !commands.empty(); });
      published = false;
      pending.swap(commands);
    }
    for (auto& command : pending) deferred.push_back(std::move(command));
    auto const view = session.View();
    if (view.ready && view.local_uid != last_uid) {
      last_uid = view.local_uid;
      std::cout << "UID " << last_uid << std::endl;
    }
    while (!deferred.empty()) {
      auto const& command = deferred.front();
      if (command == "stop") { running = false; break; }
      if (!view.ready) break;
      if (command == "host") session.CreateRoom();
      else if (command.starts_with("join ")) session.JoinRoom(command.substr(5));
      else if (command.starts_with("send ")) session.Send(command.substr(5));
      else std::cerr << "Unknown command\n";
      deferred.pop_front();
    }
    if (view.messages.size() < shown) shown = 0;
    for (; shown < view.messages.size(); ++shown)
      std::cout << "MESSAGE " << view.messages[shown] << std::endl;
  }
  session.Stop();
  input.join();
}
