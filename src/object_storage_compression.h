#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace apptraverse::storage_compression {

using Bytes = std::vector<std::uint8_t>;
using Input = std::span<std::uint8_t const>;
using Output = std::span<std::uint8_t>;

bool CompressEmbedded(Input input, Bytes& output);
bool ExpandEmbedded(Input input, Output output);
bool CompressPersistence(Input input, Bytes& output);
bool ExpandPersistence(Input input, Output output);

// UpdateLab component delivery uses the same fixed raw LZMA1 representation.
bool CompressLzma(Input input, Bytes& output);
bool ExpandLzma(Input input, Output output);

}  // namespace apptraverse::storage_compression
