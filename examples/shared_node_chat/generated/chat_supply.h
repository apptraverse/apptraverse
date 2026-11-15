#ifndef APPTRAVERSE_EMBEDDED_CHAT_SUPPLY_H_
#define APPTRAVERSE_EMBEDDED_CHAT_SUPPLY_H_

#include <cstddef>
#include <cstdint>

#include "apptraverse/embedded_distilled_domain.h"

namespace apptraverse {
namespace embedded_distilled {
namespace chat {

inline constexpr std::uint8_t kEmptyLayerByte = 0;

inline constexpr std::uint8_t kLayerBytes0[] = {
    0x00, 0x00, 0x00, 0x00, 0x02
};

inline constexpr std::uint8_t kLayerBytes1[] = {
    0x01, 0x12, 0xbb, 0x8a, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes2[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes3[] = {
    0x00, 0x00, 0x00, 0x00, 0x02
};

inline constexpr std::uint8_t kLayerBytes4[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes5[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr apptraverse::EmbeddedDistilledLayerRecord kLayers[] = {
    {2046820353u, 2178182515u, static_cast<std::uint8_t>(0u), kLayerBytes0, sizeof(kLayerBytes0)},
    {2046820353u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes1, sizeof(kLayerBytes1)},
    {2046820353u, 4294142279u, static_cast<std::uint8_t>(0u), kLayerBytes2, sizeof(kLayerBytes2)},
    {2327515649u, 2178182515u, static_cast<std::uint8_t>(0u), kLayerBytes3, sizeof(kLayerBytes3)},
    {2327515649u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes4, sizeof(kLayerBytes4)},
    {2327515649u, 4294142279u, static_cast<std::uint8_t>(0u), kLayerBytes5, sizeof(kLayerBytes5)},
};

inline constexpr apptraverse::EmbeddedDistilledSupply kSupplyConstants = {nullptr, 0};

inline constexpr apptraverse::EmbeddedObjectScopeRecord kObjectScopes[] = {
    {2046820353u, 0u, 0u, false},
    {2327515649u, 0u, 0u, false},
};

inline constexpr apptraverse::EmbeddedDistilledGraph kEmbeddedGraph = {2046820353u, kSupplyConstants, kLayers, 6, kObjectScopes, 2};

}  // namespace chat
}  // namespace embedded_distilled
}  // namespace apptraverse

#endif  // APPTRAVERSE_EMBEDDED_CHAT_SUPPLY_H_
