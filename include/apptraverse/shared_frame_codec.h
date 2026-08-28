#ifndef APPTRAVERSE_SHARED_FRAME_CODEC_H_
#define APPTRAVERSE_SHARED_FRAME_CODEC_H_

#include <cstdint>
#include <string>
#include <vector>

#include "apptraverse/shared_transport.h"

namespace apptraverse {

std::vector<std::uint8_t> EncodeSharedEventFrame(SharedEventFrame const& frame);
bool DecodeSharedEventFrame(std::vector<std::uint8_t> const& bytes,
                            SharedEventFrame& out);

std::vector<std::uint8_t> EncodeSharedAckFrame(SharedAckFrame const& frame);
bool DecodeSharedAckFrame(std::vector<std::uint8_t> const& bytes,
                          SharedAckFrame& out);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_FRAME_CODEC_H_
