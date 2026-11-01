#include "minimal_app_android_checkpoint_test_gate.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

namespace apptraverse::example::minimal {
namespace {

std::mutex gate_mu;
std::condition_variable gate_cv;
bool gate_enabled = false;
bool gate_open = true;
int persist_gate_entry_count = 0;

std::string gate_files_root;
std::filesystem::path GateFilePath() {
  return std::filesystem::path(gate_files_root) / ".checkpoint_persist_test_gate";
}

struct GateFileState {
  bool enabled = false;
  bool open = true;
  int entries = 0;
};

bool ReadGateFile(GateFileState* out) {
  if (gate_files_root.empty() || out == nullptr) {
    return false;
  }
  std::ifstream in(GateFilePath());
  if (!in) {
    return false;
  }
  int enabled = 0;
  int open = 1;
  int entries = 0;
  if (!(in >> enabled >> open >> entries)) {
    return false;
  }
  out->enabled = enabled != 0;
  out->open = open != 0;
  out->entries = entries;
  return true;
}

void WriteGateFile(GateFileState const& state) {
  if (gate_files_root.empty()) {
    return;
  }
  std::ofstream out(GateFilePath(), std::ios::trunc);
  if (!out) {
    return;
  }
  out << (state.enabled ? 1 : 0) << ' ' << (state.open ? 1 : 0) << ' '
      << state.entries << '\n';
}

void RemoveGateFile() {
  if (gate_files_root.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::remove(GateFilePath(), ec);
}

void EnableInMemory() {
  std::lock_guard<std::mutex> lock{gate_mu};
  gate_enabled = true;
  gate_open = false;
}

void DisableInMemory() {
  std::lock_guard<std::mutex> lock{gate_mu};
  gate_enabled = false;
  gate_open = true;
  persist_gate_entry_count = 0;
  gate_cv.notify_all();
}

void OpenInMemory() {
  std::lock_guard<std::mutex> lock{gate_mu};
  gate_open = true;
  gate_cv.notify_all();
}

void WaitInMemory() {
  std::unique_lock<std::mutex> lock{gate_mu};
  if (gate_enabled) {
    ++persist_gate_entry_count;
    gate_cv.notify_all();
  }
  gate_cv.wait(lock, [] { return !gate_enabled || gate_open; });
}

int EntryCountInMemory() {
  std::lock_guard<std::mutex> lock{gate_mu};
  return persist_gate_entry_count;
}

bool WaitEntriesInMemory(int expected, std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock{gate_mu};
  return gate_cv.wait_for(lock, timeout, [&] {
    return persist_gate_entry_count >= expected;
  });
}

void WaitOnGateFile() {
  GateFileState state;
  if (!ReadGateFile(&state) || !state.enabled) {
    return;
  }
  GateFileState updated = state;
  ++updated.entries;
  WriteGateFile(updated);
  auto const deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
  while (std::chrono::steady_clock::now() < deadline) {
    GateFileState current;
    if (!ReadGateFile(&current) || !current.enabled || current.open) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

}  // namespace

void SetAndroidCheckpointPersistTestGateRoot(char const* files_dir) {
  if (files_dir == nullptr) {
    gate_files_root.clear();
    return;
  }
  gate_files_root = files_dir;
}

void EnableAndroidCheckpointPersistTestGate() {
  if (!gate_files_root.empty()) {
    GateFileState state;
    state.enabled = true;
    state.open = false;
    state.entries = 0;
    WriteGateFile(state);
    return;
  }
  EnableInMemory();
}

void DisableAndroidCheckpointPersistTestGate() {
  if (!gate_files_root.empty()) {
    RemoveGateFile();
    return;
  }
  DisableInMemory();
}

void OpenAndroidCheckpointPersistTestGate() {
  if (!gate_files_root.empty()) {
    GateFileState state;
    if (!ReadGateFile(&state)) {
      return;
    }
    state.open = true;
    WriteGateFile(state);
    return;
  }
  OpenInMemory();
}

void WaitAndroidCheckpointPersistTestGateIfEnabled() {
  if (!gate_files_root.empty()) {
    WaitOnGateFile();
    return;
  }
  WaitInMemory();
}

int GetAndroidCheckpointPersistGateEntryCount() {
  if (!gate_files_root.empty()) {
    GateFileState state;
    if (!ReadGateFile(&state)) {
      return 0;
    }
    return state.entries;
  }
  return EntryCountInMemory();
}

bool WaitAndroidCheckpointPersistGateEntriesAtLeast(
    int expected, std::chrono::milliseconds timeout) {
  if (!gate_files_root.empty()) {
    auto const deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      if (GetAndroidCheckpointPersistGateEntryCount() >= expected) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
  }
  return WaitEntriesInMemory(expected, timeout);
}

}  // namespace apptraverse::example::minimal
