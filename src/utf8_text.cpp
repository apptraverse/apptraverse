#include "apptraverse/utf8_text.h"

#include <algorithm>

namespace apptraverse {
namespace {

bool IsContinuationByte(unsigned char b) { return (b & 0xC0) == 0x80; }

bool DecodeOneUtf8(std::string_view text, std::size_t& pos, std::uint32_t& out_cp) {
  if (pos >= text.size()) {
    return false;
  }
  unsigned char const b0 = static_cast<unsigned char>(text[pos]);
  std::size_t len = 0;
  std::uint32_t cp = 0;
  if (b0 <= 0x7F) {
    len = 1;
    cp = b0;
  } else if ((b0 & 0xE0) == 0xC0) {
    len = 2;
    cp = b0 & 0x1F;
  } else if ((b0 & 0xF0) == 0xE0) {
    len = 3;
    cp = b0 & 0x0F;
  } else if ((b0 & 0xF8) == 0xF0) {
    len = 4;
    cp = b0 & 0x07;
  } else {
    return false;
  }
  if (pos + len > text.size()) {
    return false;
  }
  for (std::size_t i = 1; i < len; ++i) {
    unsigned char const bx = static_cast<unsigned char>(text[pos + i]);
    if (!IsContinuationByte(bx)) {
      return false;
    }
    cp = (cp << 6) | (bx & 0x3F);
  }
  if (len == 2 && cp < 0x80) {
    return false;
  }
  if (len == 3 && cp < 0x800) {
    return false;
  }
  if (len == 4 && cp < 0x10000) {
    return false;
  }
  if (cp > 0x10FFFF) {
    return false;
  }
  if (cp >= 0xD800 && cp <= 0xDFFF) {
    return false;
  }
  out_cp = cp;
  pos += len;
  return true;
}

}  // namespace

bool IsValidUtf8(std::string_view text) {
  std::size_t pos = 0;
  while (pos < text.size()) {
    std::uint32_t cp = 0;
    if (!DecodeOneUtf8(text, pos, cp)) {
      return false;
    }
  }
  return true;
}

bool IsUtf8Boundary(std::string_view text, std::size_t byte_offset) {
  if (byte_offset > text.size()) {
    return false;
  }
  if (byte_offset == text.size()) {
    return true;
  }
  return !IsContinuationByte(static_cast<unsigned char>(text[byte_offset]));
}

std::size_t SnapForwardToUtf8Boundary(std::string_view text, std::size_t byte_offset) {
  byte_offset = std::min(byte_offset, text.size());
  while (byte_offset < text.size() &&
         IsContinuationByte(static_cast<unsigned char>(text[byte_offset]))) {
    ++byte_offset;
  }
  return byte_offset;
}

std::size_t SnapBackwardToUtf8Boundary(std::string_view text, std::size_t byte_offset) {
  byte_offset = std::min(byte_offset, text.size());
  if (byte_offset == text.size()) {
    return byte_offset;
  }
  while (byte_offset > 0 &&
         IsContinuationByte(static_cast<unsigned char>(text[byte_offset]))) {
    --byte_offset;
  }
  return byte_offset;
}

std::size_t PreviousUtf8CodePointBoundary(std::string_view text, std::size_t byte_offset) {
  byte_offset = SnapBackwardToUtf8Boundary(text, byte_offset);
  if (byte_offset == 0) {
    return 0;
  }
  std::size_t pos = 0;
  std::size_t prev_start = 0;
  while (pos < byte_offset) {
    prev_start = pos;
    std::uint32_t cp = 0;
    if (!DecodeOneUtf8(text, pos, cp)) {
      return SnapBackwardToUtf8Boundary(text, byte_offset);
    }
  }
  return prev_start;
}

std::size_t NextUtf8CodePointBoundary(std::string_view text, std::size_t byte_offset) {
  byte_offset = SnapForwardToUtf8Boundary(text, byte_offset);
  if (byte_offset >= text.size()) {
    return text.size();
  }
  std::size_t pos = byte_offset;
  std::uint32_t cp = 0;
  if (!DecodeOneUtf8(text, pos, cp)) {
    return text.size();
  }
  return pos;
}

std::uint32_t CountUtf8CodePoints(std::string_view text) {
  std::uint32_t count = 0;
  std::size_t pos = 0;
  while (pos < text.size()) {
    std::uint32_t cp = 0;
    if (!DecodeOneUtf8(text, pos, cp)) {
      break;
    }
    ++count;
  }
  return count;
}

}  // namespace apptraverse
