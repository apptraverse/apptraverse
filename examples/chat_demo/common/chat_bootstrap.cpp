#include "chat_bootstrap.h"

#include "aether-miscpp/serialization/binary_archive.h"

namespace apptraverse::example::chat_demo {
namespace {

using ae::seri::BinaryArchive;
using ae::seri::BinaryVectorBuffer;

}  // namespace

bool EncodeChatBootstrap(ChatBootstrapMessage const& message,
                         std::vector<std::uint8_t>& out) {
  out.clear();
  BinaryVectorBuffer buffer{out};
  BinaryArchive archive{std::move(buffer)};
  auto const kind = static_cast<std::uint8_t>(message.kind);
  if (!archive.Save(kind).IsOk() || !archive.Save(message.attempt_id).IsOk() ||
      !archive.Save(message.room_id).IsOk() ||
      !archive.Save(message.rejection_reason).IsOk()) {
    out.clear();
    return false;
  }
  return !out.empty() && out.size() <= kMaxControlPayloadBytes;
}

bool DecodeChatBootstrap(std::span<std::uint8_t const> bytes,
                         ChatBootstrapMessage& out) {
  if (bytes.empty() || bytes.size() > kMaxControlPayloadBytes) {
    return false;
  }
  std::vector<std::uint8_t> payload_copy(bytes.begin(), bytes.end());
  BinaryVectorBuffer buffer{payload_copy};
  BinaryArchive archive{std::move(buffer)};
  std::uint8_t kind = 0;
  ChatBootstrapMessage parsed;
  if (!archive.Load(kind).IsOk() || !archive.Load(parsed.attempt_id).IsOk() ||
      !archive.Load(parsed.room_id).IsOk() ||
      !archive.Load(parsed.rejection_reason).IsOk()) {
    return false;
  }
  std::uint8_t extra = 0;
  if (archive.Load(extra).IsOk()) {
    return false;
  }
  if (kind != static_cast<std::uint8_t>(ChatBootstrapKind::kJoinRequest) &&
      kind != static_cast<std::uint8_t>(ChatBootstrapKind::kJoinAccepted) &&
      kind != static_cast<std::uint8_t>(ChatBootstrapKind::kJoinRejected)) {
    return false;
  }
  parsed.kind = static_cast<ChatBootstrapKind>(kind);
  if (parsed.kind == ChatBootstrapKind::kJoinRejected) {
    if (parsed.rejection_reason.empty()) {
      return false;
    }
  } else if (!parsed.rejection_reason.empty()) {
    return false;
  }
  if (parsed.kind == ChatBootstrapKind::kJoinAccepted &&
      !parsed.room_id.is_valid()) {
    return false;
  }
  if (!parsed.attempt_id.is_valid()) {
    return false;
  }
  out = std::move(parsed);
  return true;
}

}  // namespace apptraverse::example::chat_demo
