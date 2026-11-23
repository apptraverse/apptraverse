#include "object_storage_compression.h"

#include <cstdlib>
#include <limits>

#include <LzmaDec.h>
#include <LzmaEnc.h>
#include <lz4.h>
#define Z_SOLO
#define Z_PREFIX
#include <zlib.h>
extern "C" {
#include <heatshrink_decoder.h>
#include <heatshrink_encoder.h>
}

#define AE_OBJECTS_CODEC_LZMA 1
#define AE_OBJECTS_CODEC_ZLIB 2
#define AE_OBJECTS_CODEC_LZ4 3
#define AE_OBJECTS_CODEC_HEATSHRINK 4
#include APPTRAVERSE_OBJECT_USER_CONFIG
#ifndef AE_OBJECTS_EMBEDDED_COMPRESSION
#define AE_OBJECTS_EMBEDDED_COMPRESSION AE_OBJECTS_CODEC_LZMA
#endif
#ifndef AE_OBJECTS_PERSISTENCE_COMPRESSION
#define AE_OBJECTS_PERSISTENCE_COMPRESSION AE_OBJECTS_CODEC_LZ4
#endif

namespace apptraverse::storage_compression {
namespace {

void* Allocate(ISzAllocPtr, std::size_t size) { return std::malloc(size); }
void Free(ISzAllocPtr, void* memory) { std::free(memory); }
constexpr ISzAlloc kAllocator{Allocate, Free};

[[maybe_unused]] voidpf AllocateZlib(voidpf, uInt items, uInt size) {
  return std::calloc(items, size);
}
[[maybe_unused]] void FreeZlib(voidpf, voidpf memory) { std::free(memory); }

template <class State, class Sink, class Poll, class Finish>
bool TransferHeatshrink(State* state, Input input, Output output,
                        std::size_t& produced, Sink sink, Poll poll,
                        Finish finish) {
  auto drain = [&] {
    for (;;) {
      // Leave one byte of scratch space to detect output beyond the limit.
      std::uint8_t extra{};
      auto space = output.subspan(produced);
      std::size_t count{};
      auto status = poll(state, space.empty() ? &extra : space.data(),
                         space.empty() ? 1 : space.size(), &count);
      if (status < 0 || count > space.size()) return false;
      produced += count;
      if (status == 0) return true;
      if (count == 0) return false;
    }
  };
  while (!input.empty()) {
    std::size_t consumed{};
    // The C API predates const-correct input, but sink only copies these bytes.
    if (sink(state, const_cast<std::uint8_t*>(input.data()), input.size(),
             &consumed) < 0 || !drain() || consumed == 0) return false;
    input = input.subspan(consumed);
  }
  for (;;) {
    auto status = finish(state);
    if (status <= 0) return status == 0;
    if (!drain()) return false;
  }
}

template <int Codec>
bool Compress(Input input, Bytes& output) {
  if constexpr (Codec == AE_OBJECTS_CODEC_LZMA) {
    return CompressLzma(input, output);
  } else if constexpr (Codec == AE_OBJECTS_CODEC_LZ4) {
    if (input.size() > LZ4_MAX_INPUT_SIZE) return false;
    auto const size = static_cast<int>(input.size());
    output.resize(LZ4_compressBound(size));
    auto const written = LZ4_compress_default(
        reinterpret_cast<char const*>(input.data()),
        reinterpret_cast<char*>(output.data()), size,
        static_cast<int>(output.size()));
    if (written <= 0) return false;
    output.resize(written);
    return true;
  } else if constexpr (Codec == AE_OBJECTS_CODEC_ZLIB) {
    z_stream stream{};
    stream.zalloc = AllocateZlib;
    stream.zfree = FreeZlib;
    if (input.size() > std::numeric_limits<uInt>::max() ||
        deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 12, 3,
                     Z_DEFAULT_STRATEGY) != Z_OK) return false;
    auto const capacity = deflateBound(&stream, static_cast<uLong>(input.size()));
    if (capacity > std::numeric_limits<uInt>::max()) {
      deflateEnd(&stream);
      return false;
    }
    output.resize(capacity);
    stream.next_in = const_cast<Bytef*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    auto const status = deflate(&stream, Z_FINISH);
    auto const written = stream.total_out;
    deflateEnd(&stream);
    output.resize(written);
    return status == Z_STREAM_END;
  } else {
    static_assert(Codec == AE_OBJECTS_CODEC_HEATSHRINK,
                  "Unknown object storage codec in user_config.h");
    auto* encoder = heatshrink_encoder_alloc(8, 4);
    if (encoder == nullptr) return false;
    output.resize(input.size() + input.size() / 8 + 1);
    std::size_t produced{};
    auto const ok = TransferHeatshrink(
        encoder, input, output, produced, heatshrink_encoder_sink,
        heatshrink_encoder_poll, heatshrink_encoder_finish);
    heatshrink_encoder_free(encoder);
    output.resize(produced);
    return ok;
  }
}

template <int Codec>
bool Expand(Input input, Output output) {
  if constexpr (Codec == AE_OBJECTS_CODEC_LZMA) {
    return ExpandLzma(input, output);
  } else if constexpr (Codec == AE_OBJECTS_CODEC_LZ4) {
    if (input.size() > std::numeric_limits<int>::max() ||
        output.size() > std::numeric_limits<int>::max()) return false;
    return LZ4_decompress_safe(reinterpret_cast<char const*>(input.data()),
                              reinterpret_cast<char*>(output.data()),
                              static_cast<int>(input.size()),
                              static_cast<int>(output.size())) ==
           static_cast<int>(output.size());
  } else if constexpr (Codec == AE_OBJECTS_CODEC_ZLIB) {
    z_stream stream{};
    stream.zalloc = AllocateZlib;
    stream.zfree = FreeZlib;
    if (input.size() > std::numeric_limits<uInt>::max() ||
        output.size() > std::numeric_limits<uInt>::max() ||
        inflateInit2(&stream, 12) != Z_OK) return false;
    stream.next_in = const_cast<Bytef*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    auto const status = inflate(&stream, Z_FINISH);
    auto const ok = status == Z_STREAM_END && stream.avail_in == 0 &&
                    stream.avail_out == 0;
    inflateEnd(&stream);
    return ok;
  } else {
    static_assert(Codec == AE_OBJECTS_CODEC_HEATSHRINK,
                  "Unknown object storage codec in user_config.h");
    auto* decoder = heatshrink_decoder_alloc(64, 8, 4);
    if (decoder == nullptr) return false;
    std::size_t produced{};
    auto const ok = TransferHeatshrink(
        decoder, input, output, produced, heatshrink_decoder_sink,
        heatshrink_decoder_poll, heatshrink_decoder_finish);
    heatshrink_decoder_free(decoder);
    return ok && produced == output.size();
  }
}

}  // namespace

bool CompressLzma(Input input, Bytes& output) {
  CLzmaEncProps props;
  LzmaEncProps_Init(&props);
  props.level = 9;
  props.fb = 273;
  props.numThreads = 1;
  props.reduceSize = input.size();
  SizeT capacity = input.size() + input.size() / 3 + 128;
  const std::uint8_t empty{};
  output.resize(LZMA_PROPS_SIZE + capacity);
  SizeT properties = LZMA_PROPS_SIZE;
  auto const status = LzmaEncode(
      output.data() + LZMA_PROPS_SIZE, &capacity,
      input.empty() ? &empty : input.data(), input.size(), &props,
      output.data(), &properties, 1, nullptr, &kAllocator, &kAllocator);
  if (status != SZ_OK || properties != LZMA_PROPS_SIZE) return false;
  output.resize(properties + capacity);
  return true;
}

bool ExpandLzma(Input input, Output output) {
  if (input.size() <= LZMA_PROPS_SIZE) return false;
  SizeT consumed = input.size() - LZMA_PROPS_SIZE;
  SizeT produced = output.size();
  std::uint8_t empty{};
  ELzmaStatus status;
  auto const result = LzmaDecode(
      output.empty() ? &empty : output.data(), &produced,
      input.data() + LZMA_PROPS_SIZE, &consumed, input.data(), LZMA_PROPS_SIZE,
      LZMA_FINISH_END, &status, &kAllocator);
  return result == SZ_OK && status == LZMA_STATUS_FINISHED_WITH_MARK &&
         consumed == input.size() - LZMA_PROPS_SIZE && produced == output.size();
}

bool CompressEmbedded(Input input, Bytes& output) {
  return Compress<AE_OBJECTS_EMBEDDED_COMPRESSION>(input, output);
}
bool ExpandEmbedded(Input input, Output output) {
  return Expand<AE_OBJECTS_EMBEDDED_COMPRESSION>(input, output);
}
bool CompressPersistence(Input input, Bytes& output) {
  return Compress<AE_OBJECTS_PERSISTENCE_COMPRESSION>(input, output);
}
bool ExpandPersistence(Input input, Output output) {
  return Expand<AE_OBJECTS_PERSISTENCE_COMPRESSION>(input, output);
}

}  // namespace apptraverse::storage_compression
