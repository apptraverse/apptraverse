#include "messenger_aether_uid.h"

#include "aether/types/uid.h"
#include "aether-miscpp/format/format.h"

namespace apptraverse {
namespace {

std::string TrimAsciiWhitespace(std::string_view raw) {
  std::size_t begin = 0;
  while (begin < raw.size() &&
         (raw[begin] == ' ' || raw[begin] == '\t' || raw[begin] == '\r' ||
          raw[begin] == '\n')) {
    ++begin;
  }
  std::size_t end = raw.size();
  while (end > begin && (raw[end - 1] == ' ' || raw[end - 1] == '\t' ||
                         raw[end - 1] == '\r' || raw[end - 1] == '\n')) {
    --end;
  }
  return std::string{raw.substr(begin, end - begin)};
}

bool IsAsciiHex(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

}  // namespace

bool TryCanonicalizeAetherUid(std::string_view raw, std::string& out) {
  std::string const trimmed = TrimAsciiWhitespace(raw);
  if (trimmed.size() != 36) {
    return false;
  }
  bool all_zero = true;
  for (std::size_t i = 0; i < trimmed.size(); ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (trimmed[i] != '-') {
        return false;
      }
      continue;
    }
    if (!IsAsciiHex(trimmed[i])) {
      return false;
    }
    if (trimmed[i] != '0') {
      all_zero = false;
    }
  }
  if (all_zero) {
    return false;
  }
  ae::UidString const uid_str{std::string_view{trimmed}};
  if (!uid_str.valid) {
    return false;
  }
  auto const uid = ae::Uid::FromString(uid_str);
  if (uid.empty()) {
    return false;
  }
  out = ae::Format("{}", uid);
  return !out.empty();
}

}  // namespace apptraverse
