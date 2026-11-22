#include "install.h"
#include <chrono>
#include <iostream>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#endif
using namespace update_lab;
namespace {
struct Child {
#ifdef _WIN32
  HANDLE process{nullptr};
  static std::wstring Quote(std::string const& value) {
    fs::path path{value}; auto text = path.wstring();
    std::wstring out{L"\""}; unsigned slashes = 0;
    for (auto c : text) {
      if (c == L'\\') { ++slashes; continue; }
      if (c == L'"') { out.append(slashes * 2 + 1, L'\\'); out += c; }
      else { out.append(slashes, L'\\'); out += c; }
      slashes = 0;
    }
    out.append(slashes * 2, L'\\'); out += L'"'; return out;
  }
  bool Start(fs::path const& exe, std::vector<std::string> const& args) {
    std::wstring command = Quote(exe.string());
    for (auto const& a : args) { command += L' '; command += Quote(a); }
    STARTUPINFOW startup{}; startup.cb = sizeof startup; PROCESS_INFORMATION info{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, exe.parent_path().c_str(), &startup, &info)) return false;
    process = info.hProcess; CloseHandle(info.hThread); return true;
  }
  bool Poll(int& code) {
    if (WaitForSingleObject(process, 0) != WAIT_OBJECT_0) return false;
    DWORD value; GetExitCodeProcess(process, &value); code = static_cast<int>(value); return true;
  }
  void Kill() { TerminateProcess(process, 87); WaitForSingleObject(process, INFINITE); }
  ~Child() { if (process) CloseHandle(process); }
#else
  pid_t process{-1};
  bool Start(fs::path const& exe, std::vector<std::string> const& args) {
    process = fork();
    if (process == 0) {
      std::vector<std::string> owned{exe.string()}; owned.insert(owned.end(), args.begin(), args.end());
      std::vector<char*> argv; for (auto& a : owned) argv.push_back(a.data()); argv.push_back(nullptr);
      execv(exe.c_str(), argv.data()); _exit(127);
    }
    return process > 0;
  }
  bool Poll(int& code) {
    int status; auto result = waitpid(process, &status, WNOHANG);
    if (result != process) return false;
    code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status); return true;
  }
  void Kill() { kill(process, SIGKILL); int status; waitpid(process, &status, 0); }
#endif
};
}
int main(int argc, char** argv) {
  fs::path install, profile, seed;
  unsigned timeout_ms = 30000;
  std::vector<std::string> forwarded;
  bool suppress_health = false;
  for (int n = 1; n < argc; ++n) {
    std::string arg = argv[n];
    if (arg == "--install" && n + 1 < argc) install = fs::absolute(argv[++n]);
    else if (arg == "--profile" && n + 1 < argc) profile = fs::absolute(argv[++n]);
    else if (arg == "--seed" && n + 1 < argc) seed = fs::absolute(argv[++n]);
    else if (arg == "--health-timeout-ms" && n + 1 < argc) timeout_ms = static_cast<unsigned>(std::stoul(argv[++n]));
    else if (arg == "--suppress-candidate-health") suppress_health = true;
    else forwarded.push_back(std::move(arg));
  }
  if (install.empty() || profile.empty()) Fail("launcher requires --install and --profile");
  FileLock lock{install / "launcher.lock"};
  if (!lock.acquired()) Fail("this installation is already running");
  {
    FileLock application{profile / "application.lock"};
    if (!application.acquired()) Fail("application is still running; installation state was not changed");
  }
  Installation state;
  if (!LoadInstallation(install, state)) {
    if (seed.empty()) Fail("first launch requires --seed <E0 executable>");
    auto path = install / "slots" / "E0" / BinaryName();
    if (!CopyAtomic(seed, path)) Fail("seed installation failed");
#ifndef _WIN32
    std::error_code ec; fs::permissions(path, fs::perms::owner_exec, fs::perm_options::add, ec);
    if (ec) Fail("seed chmod failed");
#endif
    state = {"stable", "E0", "", "", 0, 0};
    if (!SaveInstallation(install, state)) Fail("installation write failed");
  }
  bool rolled_back = false, no_action = false;
  auto rollback = [&] {
    FileLock application{profile / "application.lock"};
    if (!application.acquired()) Fail("cannot roll back while an application still owns this profile");
    if (!RestoreProfile(install, profile)) Fail("profile restore failed");
    state.phase = "stable"; state.candidate.clear();
    if (!SaveInstallation(install, state)) Fail("rollback journal write failed");
    rolled_back = true; no_action = true;
    std::cerr << "UPDATE_ROLLED_BACK " << state.active << '\n';
  };
  // An interrupted trial is never silently promoted on the next boot.
  if (state.phase == "trial") rollback();
  for (unsigned launches = 0; launches < 10; ++launches) {
    bool trial = state.phase == "staged";
    std::string slot = trial ? state.candidate : state.active;
    if (trial) { state.phase = "trial"; if (!SaveInstallation(install, state)) Fail("trial journal write failed"); }
    std::string token = std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    auto health = install / "health" / (token + ".ready");
    std::vector<std::string> args = forwarded;
    args.insert(args.end(), {"--install", install.string(), "--profile", profile.string(), "--health-file", health.string(), "--health-token", token});
    if (no_action) args.push_back("--no-action");
    if (trial && suppress_health) args.push_back("--suppress-health");
    Child child;
    if (!child.Start(install / "slots" / slot / BinaryName(), args)) {
      if (trial) { rollback(); continue; }
      Fail("application process could not be started");
    }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{timeout_ms};
    bool healthy = false, exited = false;
    int code = 0;
    while (true) {
      Bytes ack;
      if (!healthy && ReadFile(health, ack) && std::string{ack.begin(), ack.end()} == token) {
        healthy = true;
        if (trial) {
          state.active = state.candidate; state.active_version = state.candidate_version;
          state.candidate.clear(); state.phase = "stable";
          if (!SaveInstallation(install, state)) Fail("health commit failed");
          std::cerr << "UPDATE_HEALTHY " << state.active << '\n';
        }
      }
      if (child.Poll(code)) { exited = true; break; }
      if (trial && !healthy && std::chrono::steady_clock::now() >= deadline) { child.Kill(); code = 87; break; }
      std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    (void)exited;
    std::error_code ec; fs::remove(health, ec);
    if (trial && !healthy) { rollback(); continue; }
    if (!LoadInstallation(install, state)) Fail("installation reread failed");
    if (trial && code != 0 && code != 42) {
      state.active = state.previous;
      state.active_version = static_cast<unsigned>(std::stoul(state.active.substr(1)));
      rollback(); continue;
    }
    if (code == 42) { no_action = true; continue; }
    return rolled_back ? 3 : code;
  }
  Fail("too many consecutive executable transitions");
}
