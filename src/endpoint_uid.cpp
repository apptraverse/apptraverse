#include "apptraverse/endpoint_uid.h"

#include <charconv>
#include <cstring>

namespace apptraverse {

ae::Uid LoadLegacyEndpointUidForMigration(std::string const& label) {
  if (label.empty()) {
    return {};
  }
  ae::UidString const as_rfc{std::string_view{label}};
  if (as_rfc.valid) {
    return ae::Uid::FromString(as_rfc);
  }
  std::array<std::uint8_t, ae::Uid::kSize> bytes{};
  for (std::size_t i = 0; i < label.size() && i < bytes.size(); ++i) {
    bytes[i] = static_cast<std::uint8_t>(label[i]);
  }
  return ae::Uid{bytes};
}

std::string FormatEndpointUid(ae::Uid uid) {
  if (uid.empty()) {
    return {};
  }
  constexpr std::uint8_t kMinTwoCharsValue = 0x10;
  constexpr int kPrintBase = 16;
  std::array<char, (ae::Uid::kSize * 2) + 4> buff{};
  std::size_t wp = 0;
  for (std::size_t i = 0; i < ae::Uid::kSize; ++i) {
    switch (i) {
      case 4:
      case 6:
      case 8:
      case 10:
        buff[wp++] = '-';
        break;
      default:
        break;
    }
    auto const v = uid.value[i];
    if (v < kMinTwoCharsValue) {
      buff[wp++] = '0';
      std::to_chars(buff.data() + wp, buff.data() + wp + 1, v, kPrintBase);
      wp += 1;
    } else {
      std::to_chars(buff.data() + wp, buff.data() + wp + 2, v, kPrintBase);
      wp += 2;
    }
  }
  return std::string(buff.data(), wp);
}

ae::Uid ParseEndpointUid(std::string_view text) {
  if (text.empty()) {
    return {};
  }
  ae::UidString const as_rfc{text};
  if (!as_rfc.valid) {
    return {};
  }
  return ae::Uid::FromString(as_rfc);
}

ae::Uid MemoryTestEndpointUid(std::uint8_t tag) {
  std::array<std::uint8_t, ae::Uid::kSize> bytes{};
  bytes[15] = tag;
  return ae::Uid{bytes};
}

bool EndpointMatchesTransport(ae::Uid const& endpoint,
                              std::string const& transport_text) {
  if (transport_text.empty()) {
    return endpoint.empty();
  }
  ae::Uid const parsed = ParseEndpointUid(transport_text);
  if (!parsed.empty()) {
    return parsed == endpoint;
  }
  if (LoadLegacyEndpointUidForMigration(transport_text) == endpoint) {
    return true;
  }
  return FormatEndpointUid(endpoint) == transport_text;
}

}  // namespace apptraverse
