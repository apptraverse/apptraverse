#include "wire.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace update_lab {
std::optional<Frame> FrameAssembler::Receive(FramePiece const& piece) {
  auto& assembly = pending_[piece.request];
  if (assembly.bytes.empty()) { assembly.bytes.resize(piece.size); assembly.seen.resize(piece.size, false); }
  for (std::size_t i = 0; i < piece.bytes.size(); ++i) {
    auto position = piece.offset + i;
    if (assembly.seen[position]) continue;
    assembly.seen[position] = true; assembly.bytes[position] = piece.bytes[i]; ++assembly.received;
  }
  if (assembly.received == assembly.bytes.size()) {
    Frame frame;
    bool loaded = Decode(assembly.bytes, frame);
    pending_.erase(piece.request);
    if (!loaded) Fail("native transport frame decode failed");
    return frame;
  }
  while (pending_.size() > 64) pending_.erase(pending_.begin());
  return {};
}
std::size_t FrameAssembler::buffered() const {
  std::size_t count = 0;
  for (auto const& [id, assembly] : pending_) count += assembly.received;
  return count;
}
[[noreturn]] void Fail(std::string const& message) {
  std::fprintf(stderr, "UpdateLab: %s\n", message.c_str());
  std::exit(1);
}
bool ReadFile(fs::path const& path, Bytes& bytes, std::uint64_t max) {
  std::error_code ec;
  auto size = fs::file_size(path, ec);
  if (ec || size > max) return false;
  std::ifstream input{path, std::ios::binary};
  if (!input) return false;
  Bytes next(static_cast<std::size_t>(size));
  input.read(reinterpret_cast<char*>(next.data()), static_cast<std::streamsize>(size));
  if (!input) return false;
  bytes = std::move(next);
  return true;
}
bool AtomicWrite(fs::path const& path, Bytes const& bytes) {
  std::error_code ec;
  auto parent = path.has_parent_path() ? path.parent_path() : fs::path{"."};
  fs::create_directories(parent, ec);
  if (ec) return false;
  auto temp = path;
  temp += ".pending";
#ifdef _WIN32
  HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  bool ok = bytes.size() <= MAXDWORD &&
      WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
      written == bytes.size() && FlushFileBuffers(file);
  if (!CloseHandle(file)) ok = false;
  if (!ok) return false;
  // Short-lived readers (including the acceptance observer) can hold a Windows
  // rename open. Keep the committed file intact and retry only that condition.
  for (unsigned attempt = 0; attempt < 50; ++attempt) {
    if (MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    auto error = GetLastError();
    if (error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED) return false;
    Sleep(1);
  }
  return false;
#else
  int file = open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (file < 0) return false;
  std::size_t offset = 0;
  bool ok = true;
  while (offset < bytes.size()) {
    auto n = write(file, bytes.data() + offset, bytes.size() - offset);
    if (n <= 0) { ok = false; break; }
    offset += static_cast<std::size_t>(n);
  }
  if (fsync(file) != 0) ok = false;
  if (close(file) != 0) ok = false;
  if (!ok) return false;
  fs::rename(temp, path, ec);
  if (ec) return false;
  int directory = open(parent.c_str(), O_RDONLY | O_DIRECTORY);
  if (directory < 0) return false;
  ok = fsync(directory) == 0;
  close(directory);
  return ok;
#endif
}
bool CopyAtomic(fs::path const& source, fs::path const& dest) {
  Bytes bytes;
  return ReadFile(source, bytes) && AtomicWrite(dest, bytes);
}
bool ReadCatalog(Bytes const& bytes, Catalog& catalog) {
  return Decode(bytes, catalog) && catalog.format == kCatalogFormat;
}
Component const* FindComponent(Catalog const& catalog, std::string const& id) {
  for (auto const& c : catalog.components) if (c.id == id) return &c;
  return nullptr;
}
Release const* FindRelease(Catalog const& catalog, unsigned id) {
  for (auto const& r : catalog.releases) if (r.id == id) return &r;
  return nullptr;
}
bool ValidPack(LanguagePack const& p, std::string const& language) {
  return p.locale == language && p.text.size() == TextCount &&
      std::all_of(p.text.begin(), p.text.end(), [](auto const& s) { return !s.empty(); });
}
std::string Platform() {
#ifdef _WIN32
  return "windows-x64";
#else
  return "linux-x64";
#endif
}
std::string BinaryName() {
#ifdef _WIN32
  return "update_lab.exe";
#else
  return "update_lab";
#endif
}
std::string LanguageId(unsigned r, std::string const& l) {
  return "language/" + std::to_string(r) + "/" + l;
}
std::string ExecutableId(unsigned e) {
  return ExeId(Platform(), e);
}
std::string FileId(std::string id) {
  return id + ".component";
}
FileLock::FileLock(fs::path const& path) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  if (ec) return;
#ifdef _WIN32
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
      nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  acquired_ = handle != INVALID_HANDLE_VALUE;
  handle_ = reinterpret_cast<std::intptr_t>(handle);
#else
  handle_ = open(path.c_str(), O_CREAT | O_RDWR, 0600);
  acquired_ = handle_ >= 0 && flock(static_cast<int>(handle_), LOCK_EX | LOCK_NB) == 0;
#endif
}
FileLock::~FileLock() {
#ifdef _WIN32
  if (acquired_) CloseHandle(reinterpret_cast<HANDLE>(handle_));
#else
  if (handle_ >= 0) close(static_cast<int>(handle_));
#endif
}
}
