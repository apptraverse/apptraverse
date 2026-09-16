#include "chat_launch_ipc.h"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "aether-miscpp/serialization/binary_archive.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace apptraverse::example::chat_demo {
namespace {

using ae::seri::BinaryArchive;
using ae::seri::BinaryVectorBuffer;

std::uint64_t Fnv1a64(std::string_view data) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (unsigned char c : data) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::string ToLowerHex(std::uint64_t value) {
  static char const* kHex = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[static_cast<std::size_t>(i)] = kHex[value & 0xF];
    value >>= 4;
  }
  return out;
}

template <typename Buffer>
bool SaveWire(ae::seri::BinaryArchive<Buffer>& archive,
              ChatLaunchIpcPayload const& payload) {
  return archive.Save(payload.profile_key).IsOk() &&
         archive.Save(payload.open_peer.peer_admin_id).IsOk() &&
         archive.Save(payload.open_peer.peer_aether_uid).IsOk() &&
         archive.Save(payload.open_peer.peer_name).IsOk();
}

template <typename Buffer>
bool LoadWire(ae::seri::BinaryArchive<Buffer>& archive,
              ChatLaunchIpcPayload& payload) {
  return archive.Load(payload.profile_key).IsOk() &&
         archive.Load(payload.open_peer.peer_admin_id).IsOk() &&
         archive.Load(payload.open_peer.peer_aether_uid).IsOk() &&
         archive.Load(payload.open_peer.peer_name).IsOk();
}

bool FieldWithinLimit(std::string const& s, std::size_t max_len) {
  return s.size() <= max_len;
}

}  // namespace

std::string NormalizeProfileKey(std::filesystem::path const& state_dir) {
  std::error_code ec;
  std::filesystem::path absolute = std::filesystem::absolute(state_dir, ec);
  if (ec) {
    absolute = state_dir;
  }
  absolute = absolute.lexically_normal();
  return absolute.string();
}

std::string ProfileRoutingToken(std::string const& profile_key) {
  return ToLowerHex(Fnv1a64(profile_key));
}

#ifdef _WIN32
std::wstring ProfileRoutingWindowTitle(std::string const& profile_key) {
  std::string const token = ProfileRoutingToken(profile_key);
  std::wstring title = L"AppTraverseChat-";
  title.reserve(title.size() + token.size());
  for (char c : token) {
    title.push_back(static_cast<wchar_t>(c));
  }
  return title;
}

std::wstring ProfileRoutingWindowClass(std::string const& profile_key) {
  std::wstring cls = L"AppTraverseChatIpc.";
  std::string const token = ProfileRoutingToken(profile_key);
  cls.reserve(cls.size() + token.size());
  for (char c : token) {
    cls.push_back(static_cast<wchar_t>(c));
  }
  return cls;
}
#endif

bool EncodeLaunchIpcPayload(ChatLaunchIpcPayload const& payload,
                            std::vector<std::uint8_t>& out) {
  if (!FieldWithinLimit(payload.profile_key, 4096) ||
      !FieldWithinLimit(payload.open_peer.peer_admin_id, 1024) ||
      (payload.open_peer.peer_aether_uid.has_value() &&
       !FieldWithinLimit(*payload.open_peer.peer_aether_uid, 128)) ||
      (payload.open_peer.peer_name.has_value() &&
       !FieldWithinLimit(*payload.open_peer.peer_name, 1024))) {
    return false;
  }

  out.clear();
  BinaryVectorBuffer buffer{out};
  BinaryArchive archive{std::move(buffer)};
  if (!SaveWire(archive, payload)) {
    out.clear();
    return false;
  }
  return out.size() <= kLaunchIpcMaxPayloadBytes;
}

bool DecodeLaunchIpcPayload(std::vector<std::uint8_t> const& bytes,
                            ChatLaunchIpcPayload& out) {
  if (bytes.empty() || bytes.size() > kLaunchIpcMaxPayloadBytes) {
    return false;
  }
  std::vector<std::uint8_t> payload_copy = bytes;
  BinaryVectorBuffer buffer{payload_copy};
  BinaryArchive archive{std::move(buffer)};
  out = {};
  if (!LoadWire(archive, out)) {
    return false;
  }
  if (out.profile_key.empty() || out.open_peer.peer_admin_id.empty()) {
    return false;
  }
  return true;
}

LaunchIpcReply ValidateLaunchIpcPayload(ChatLaunchIpcPayload const& payload,
                                        std::string const& expected_profile_key) {
  if (payload.profile_key != expected_profile_key) {
    return LaunchIpcReply::kProfileMismatch;
  }
  if (payload.open_peer.peer_admin_id.empty()) {
    return LaunchIpcReply::kInvalidPayload;
  }
  if (!FieldWithinLimit(payload.profile_key, 4096) ||
      !FieldWithinLimit(payload.open_peer.peer_admin_id, 1024) ||
      (payload.open_peer.peer_aether_uid.has_value() &&
       !FieldWithinLimit(*payload.open_peer.peer_aether_uid, 128)) ||
      (payload.open_peer.peer_name.has_value() &&
       !FieldWithinLimit(*payload.open_peer.peer_name, 1024))) {
    return LaunchIpcReply::kOversized;
  }
  return LaunchIpcReply::kAccepted;
}

#ifdef _WIN32
ForwardLaunchResult TryForwardLaunchToPrimary(
    std::string const& profile_key, OpenPeerRequest const& open_peer,
    std::chrono::milliseconds timeout, int max_attempts) {
  ChatLaunchIpcPayload payload{.profile_key = profile_key, .open_peer = open_peer};
  std::vector<std::uint8_t> encoded;
  if (!EncodeLaunchIpcPayload(payload, encoded)) {
    return ForwardLaunchResult::kError;
  }

  std::wstring const window_class = ProfileRoutingWindowClass(profile_key);
  COPYDATASTRUCT cds{};
  cds.dwData = kLaunchIpcCopyDataMagic;
  cds.cbData = static_cast<DWORD>(encoded.size());
  cds.lpData = encoded.data();

  for (int attempt = 0; attempt < max_attempts; ++attempt) {
    HWND const hwnd = FindWindowW(window_class.c_str(), nullptr);
    if (hwnd == nullptr) {
      std::this_thread::sleep_for(std::chrono::milliseconds{100});
      continue;
    }

    DWORD window_tid = GetWindowThreadProcessId(hwnd, nullptr);
    DWORD_PTR reply = 0;
    LRESULT sent = 0;
    if (window_tid == GetCurrentThreadId()) {
      reply = SendMessageW(hwnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds));
      sent = 1;
    } else {
      sent = SendMessageTimeoutW(
          hwnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds),
          SMTO_ABORTIFHUNG | SMTO_BLOCK, static_cast<DWORD>(timeout.count()), &reply);
      if (sent == 0) {
        return ForwardLaunchResult::kTimeout;
      }
    }
    switch (static_cast<LaunchIpcReply>(reply)) {
      case LaunchIpcReply::kAccepted: {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        AllowSetForegroundWindow(pid);
        return ForwardLaunchResult::kAccepted;
      }
      case LaunchIpcReply::kRejected:
      case LaunchIpcReply::kProfileMismatch:
      case LaunchIpcReply::kInvalidPayload:
      case LaunchIpcReply::kOversized:
        return ForwardLaunchResult::kRejected;
      default:
        return ForwardLaunchResult::kError;
    }
  }
  return ForwardLaunchResult::kNoPrimary;
}
#endif

}  // namespace apptraverse::example::chat_demo
