#ifndef APPTRAVERSE_UTF8_TEXT_H_
#define APPTRAVERSE_UTF8_TEXT_H_

#include <cstdint>
#include <string>
#include <string_view>

namespace apptraverse {

// All helpers assume std::string holds UTF-8 text. Offsets are byte indices.

bool IsValidUtf8(std::string_view text);

// True when offset is 0, text.size(), or the first byte of a scalar value.
bool IsUtf8Boundary(std::string_view text, std::size_t byte_offset);

// Move forward to the next scalar boundary (never decreases offset).
std::size_t SnapForwardToUtf8Boundary(std::string_view text, std::size_t byte_offset);

// Move backward to the previous scalar boundary (never increases offset).
std::size_t SnapBackwardToUtf8Boundary(std::string_view text, std::size_t byte_offset);

// Byte offset at the start of the scalar immediately before byte_offset.
std::size_t PreviousUtf8CodePointBoundary(std::string_view text, std::size_t byte_offset);

// Byte offset at the start of the scalar immediately after byte_offset.
std::size_t NextUtf8CodePointBoundary(std::string_view text, std::size_t byte_offset);

std::uint32_t CountUtf8CodePoints(std::string_view text);

}  // namespace apptraverse

#endif  // APPTRAVERSE_UTF8_TEXT_H_
