#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>

#include "chat_session.h"

namespace {
using namespace apptraverse::chat;
using namespace std::chrono_literals;

std::int64_t Ticks() {
  LARGE_INTEGER value{};
  if (!QueryPerformanceCounter(&value)) { std::abort(); }
  return value.QuadPart;
}

std::string Quote(std::string const& text) {
  std::string result = "\"";
  for (auto c : text) {
    if (c == '"' || c == '\\') { result += '\\'; result += c; }
    else if (static_cast<unsigned char>(c) < 32) { result += ' '; }
    else { result += c; }
  }
  return result + '"';
}

void Emit(std::string const& json) { std::cout << "SNBENCH " << json << std::endl; }
void Stamp(char const* event, std::uint64_t seq, std::int64_t ticks) {
  Emit("{\"event\":" + Quote(event) + ",\"seq\":" + std::to_string(seq) +
       ",\"ticks\":" + std::to_string(ticks) + "}");
}

std::uint64_t Sequence(std::string const& row) {
  auto const begin = row.find("SNBENCH:");
  if (begin == std::string::npos) { return 0; }
  auto const first = row.data() + begin + 8;
  std::uint64_t seq = 0;
  auto const parsed = std::from_chars(first, row.data() + row.size(), seq);
  return parsed.ec == std::errc{} && parsed.ptr != row.data() + row.size() &&
      *parsed.ptr == ':' ? seq : 0;
}

std::string Argument(int argc, char** argv, std::string const& key) {
  for (int i = 1; i + 1 < argc; ++i) { if (argv[i] == key) { return argv[i + 1]; } }
  return {};
}
}  // namespace

int main(int argc, char** argv) {
  auto const profile = Argument(argc, argv, "--profile");
  auto const name = Argument(argc, argv, "--name");
  if (profile.empty() || (name != "Host" && name != "Client")) { return 2; }
  LARGE_INTEGER frequency{};
  if (!QueryPerformanceFrequency(&frequency)) { return 2; }
  Emit("{\"event\":\"clock\",\"frequency\":" + std::to_string(frequency.QuadPart) + "}");

  // One real ChatSession per process, with the same SDK, persistence and
  // publication cadence as the GUI. Only its presentation consumer is headless.
  ChatSession session{std::filesystem::absolute(profile), name};
  session.Start();
  std::mutex command_mutex;
  std::deque<std::string> commands;
  std::thread input{[&] {
    for (std::string line; std::getline(std::cin, line);) {
      { std::lock_guard lock{command_mutex}; commands.push_back(line); }
      if (line == "stop") { return; }
    }
    std::lock_guard lock{command_mutex}; commands.push_back("stop");
  }};
  auto next_display = std::chrono::steady_clock::now();
  std::string last_state, rendered;
  std::map<std::uint64_t, std::string> observed;
  std::set<std::uint64_t> presented;
  bool running = true;
  bool okay = true;
  while (running) {
    std::deque<std::string> pending;
    { std::lock_guard lock{command_mutex}; pending.swap(commands); }
    for (auto const& command : pending) {
      if (command == "stop") { running = false; break; }
      if (command == "host") { session.CreateRoom(); }
      else if (command.starts_with("join ")) { session.JoinRoom(command.substr(5)); }
      else if (command.starts_with("send ")) {
        std::istringstream args{command.substr(5)};
        std::uint64_t seq{};
        args >> seq;
        std::string message;
        std::getline(args >> std::ws, message);
        if (!seq || Sequence(message) != seq) { okay = false; running = false; break; }
        auto const started = Ticks();
        session.Send(std::move(message));
        Stamp("sent", seq, started);
      } else { okay = false; running = false; break; }
    }
    if (!running) { break; }
    auto const view = session.View();
    auto const observed_at = Ticks();
    auto const state = "{\"event\":\"state\",\"ready\":" + std::string(view.ready ? "true" : "false") +
        ",\"in_room\":" + (view.in_room ? "true" : "false") +
        ",\"uid\":" + Quote(view.local_uid) + ",\"invite\":" + Quote(view.invite) +
        ",\"members\":" + std::to_string(view.participants.size()) +
        ",\"status\":" + Quote(view.status) + "}";
    if (state != last_state) { Emit(state); last_state = state; }
    auto const remote_prefix = name == "Host" ? "Client: " : "Host: ";
    std::set<std::uint64_t> in_view;
    for (auto const& row : view.messages) {
      if (!row.starts_with(remote_prefix)) { continue; }
      auto const seq = Sequence(row);
      if (!seq) { continue; }
      if (!in_view.insert(seq).second) { okay = false; running = false; break; }
      auto const [found, fresh] = observed.emplace(seq, row);
      if (found->second != row) { okay = false; running = false; break; }
      if (fresh) {
        Emit("{\"event\":\"observed\",\"seq\":" + std::to_string(seq) +
             ",\"ticks\":" + std::to_string(observed_at) + ",\"message\":" +
             Quote(row.substr(std::char_traits<char>::length(remote_prefix))) + "}");
      }
    }
    auto const now = std::chrono::steady_clock::now();
    if (now >= next_display) {
      // Same 100 ms consumer cadence and text projection as Window::Refresh.
      // This is a virtual display, not a measurement of WM_PAINT or pixels.
      std::string text;
      for (auto const& row : view.messages) {
        if (!text.empty()) { text += "\r\n"; }
        text += row;
      }
      rendered = std::move(text);
      auto const displayed_at = Ticks();
      for (auto const seq : in_view) {
        if (presented.insert(seq).second) { Stamp("presented", seq, displayed_at); }
      }
      next_display = now + 100ms;
    }
    std::this_thread::sleep_for(1ms);
  }
  if (!okay) { Emit("{\"event\":\"error\",\"message\":\"invalid command or duplicate/corrupt message\"}"); }
  session.Stop();
  input.join();
  Emit("{\"event\":\"stopped\"}");
  return okay ? 0 : 1;
}
