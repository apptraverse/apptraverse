#ifndef APPTRAVERSE_EMBEDDED_DISTILLED_WINDOW_CATALOG_SUPPLY_H_
#define APPTRAVERSE_EMBEDDED_DISTILLED_WINDOW_CATALOG_SUPPLY_H_

#include <cstddef>
#include <cstdint>

#include "apptraverse/embedded_distilled_domain.h"

namespace apptraverse {
namespace embedded_distilled {
namespace window_catalog {

inline constexpr std::uint8_t kEmptyLayerByte = 0;

inline constexpr std::uint8_t kLayerBytes1[] = {
    0xdc, 0xc9, 0x8b, 0xdb, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes2[] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x04, 0x00, 0x00, 
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x80, 0xb4, 0x2e, 0xe9, 0x00, 
    0x28, 0x4c, 0x8c, 0x93, 0x00, 0x24, 0xb9, 0x8d, 0x6a, 0x00, 0xcb, 0xd5, 
    0x87, 0xe9, 0x00, 0x3a, 0x6e, 0x02, 0x39, 0x00, 0xc1, 0xa2, 0x97, 0x7c, 
    0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x0a, 
    0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 
    0x00, 0x00, 0xf7, 0x04, 0x64, 0xbd, 0x00, 0xa9, 0x6a, 0x93, 0xf9, 0x00, 
    0x3b, 0x0f, 0x93, 0x9a, 0x00, 0x31, 0x91, 0xf0, 0x2b, 0x00
};

inline constexpr std::uint8_t kLayerBytes3[] = {
    0x0f, 0x00, 0x00, 0x00, 0x03
};

inline constexpr std::uint8_t kLayerBytes4[] = {
    0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes5[] = {
    0x03, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes6[] = {
    0x50, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes9[] = {
    0x23, 0x66, 0xb5, 0x80, 0x00, 0x01, 0x00, 0x00, 0x00, 0xfa, 0x0c, 0x3b, 
    0x07, 0x00, 0x24, 0xe3, 0x8c, 0x2f, 0xeb, 0x5e, 0x06, 0x00, 0x24, 0xe3, 
    0x8c, 0x2f, 0xeb, 0x5e, 0x06, 0x00
};

inline constexpr std::uint8_t kLayerBytes10[] = {
    0x80, 0x02, 0x00, 0x00, 0xe0, 0x01, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes14[] = {
    0x02, 0x00, 0x00, 0x00, 0x00, 0xf7, 0xdd, 0xb0, 0x2c, 0x00, 0x80, 0xb4, 
    0x2e, 0xe9, 0x00
};

inline constexpr std::uint8_t kLayerBytes16[] = {
    0x01, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes17[] = {
    0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff
};

inline constexpr std::uint8_t kLayerBytes19[] = {
    0x17, 0x88, 0x7e, 0xca, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes20[] = {
    0x01, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes21[] = {
    0x04, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 
    0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x90, 0x01, 0x00, 0x00, 0x7c, 0x01, 
    0x00, 0x00, 0x89, 0x13, 0x00, 0x00, 0x01
};

inline constexpr std::uint8_t kLayerBytes24[] = {
    0x28, 0x4c, 0x8c, 0x93, 0x00, 0x01
};

inline constexpr std::uint8_t kLayerBytes26[] = {
    0x01, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes27[] = {
    0x61, 0x15, 0x86, 0xbb, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes28[] = {
    0x24, 0xb9, 0x8d, 0x6a, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes30[] = {
    0x01, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes31[] = {
    0x8f, 0xed, 0x91, 0x51, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes32[] = {
    0xcb, 0xd5, 0x87, 0xe9, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes34[] = {
    0x01, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes35[] = {
    0xdf, 0xb8, 0x38, 0xd9, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes36[] = {
    0x01, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes37[] = {
    0x3a, 0x6e, 0x02, 0x39, 0x00, 0x01
};

inline constexpr std::uint8_t kLayerBytes39[] = {
    0x80, 0xe1, 0x3e, 0xb7, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes40[] = {
    0x10, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes41[] = {
    0xa0, 0x00, 0x00, 0x00, 0x78, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes44[] = {
    0x20, 0x3a, 0x17, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes45[] = {
    0xe0, 0x01, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes49[] = {
    0x0f, 0x00, 0x00, 0x00, 0x00, 0xc1, 0xa2, 0x97, 0x7c, 0x00, 0x80, 0xb4, 
    0x2e, 0xe9, 0x00
};

inline constexpr std::uint8_t kLayerBytes52[] = {
    0x02, 0x00, 0x00, 0x00, 0x45, 0x4e
};

inline constexpr std::uint8_t kLayerBytes53[] = {
    0x50, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x80, 0x02, 0x00, 0x00, 
    0xe0, 0x01, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes57[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes58[] = {
    0x02, 0x00, 0x00, 0x00, 0x1b, 0xa5, 0xd4, 0xe9, 0x00, 0xa2, 0x65, 0x2e, 
    0x0d, 0x00
};

inline constexpr std::uint8_t kLayerBytes60[] = {
    0x10, 0x00, 0x00, 0x00, 0xd0, 0x94, 0xd0, 0xbe, 0xd0, 0xb1, 0xd0, 0xb0, 
    0xd0, 0xb2, 0xd0, 0xb8, 0xd1, 0x82, 0xd1, 0x8c
};

inline constexpr std::uint8_t kLayerBytes62[] = {
    0x02, 0x00, 0x00, 0x00, 0x52, 0x55
};

inline constexpr std::uint8_t kLayerBytes64[] = {
    0x0c, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x32, 0x01, 
    0x00, 0x00, 0x90, 0x01, 0x00, 0x00, 0x5a, 0x00, 0x00, 0x00, 0x1c, 0x00, 
    0x00, 0x00, 0x8d, 0x13, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes67[] = {
    0x95, 0xc3, 0x95, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes68[] = {
    0x02, 0x00, 0x00, 0x00, 0x2d, 0x33, 0x18, 0xa1, 0x01, 0xc2, 0x57, 0x07, 
    0xd9, 0x03
};

inline constexpr std::uint8_t kLayerBytes70[] = {
    0x0e, 0x00, 0x00, 0x00, 0xd0, 0x9e, 0xd1, 0x82, 0xd0, 0xba, 0xd1, 0x80, 
    0xd1, 0x8b, 0xd1, 0x82, 0xd1, 0x8c
};

inline constexpr std::uint8_t kLayerBytes72[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes73[] = {
    0x02, 0x00, 0x00, 0x00, 0x8d, 0xb9, 0xed, 0xda, 0x00, 0xc7, 0xae, 0x34, 
    0xe6, 0x00
};

inline constexpr std::uint8_t kLayerBytes75[] = {
    0x72, 0xf1, 0x1a, 0x97, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes76[] = {
    0x02, 0x00, 0x00, 0x00, 0xae, 0x90, 0xd8, 0x0d, 0x01, 0xb6, 0x4a, 0x36, 
    0x02, 0x03
};

inline constexpr std::uint8_t kLayerBytes77[] = {
    0x10, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes78[] = {
    0xa0, 0x00, 0x00, 0x00, 0x78, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes81[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes82[] = {
    0xe0, 0x01, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes84[] = {
    0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes85[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes87[] = {
    0x09, 0x00, 0x00, 0x00, 0xd0, 0x9e, 0xd0, 0xba, 0xd0, 0xbd, 0xd0, 0xbe, 
    0x20
};

inline constexpr std::uint8_t kLayerBytes88[] = {
    0x24, 0xb9, 0x8d, 0x6a, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes90[] = {
    0x00, 0x00, 0x00, 0x00, 0x02
};

inline constexpr std::uint8_t kLayerBytes91[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes93[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes94[] = {
    0x02, 0x00, 0x00, 0x00, 0x4d, 0xbd, 0xe3, 0xc0, 0x00, 0x35, 0x2d, 0x54, 
    0x4f, 0x00
};

inline constexpr std::uint8_t kLayerBytes96[] = {
    0x7c, 0x85, 0xbe, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes97[] = {
    0x02, 0x00, 0x00, 0x00, 0xf3, 0x5d, 0xf7, 0x6b, 0x01, 0x71, 0x14, 0xab, 
    0x2f, 0x03
};

inline constexpr std::uint8_t kLayerBytes99[] = {
    0x04, 0x00, 0x00, 0x00, 0x4f, 0x70, 0x65, 0x6e
};

inline constexpr std::uint8_t kLayerBytes101[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes102[] = {
    0x02, 0x00, 0x00, 0x00, 0xf3, 0x5d, 0xf7, 0x6b, 0x00, 0x71, 0x14, 0xab, 
    0x2f, 0x00
};

inline constexpr std::uint8_t kLayerBytes104[] = {
    0x3c, 0x36, 0x11, 0x68, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes105[] = {
    0x02, 0x00, 0x00, 0x00, 0x4d, 0xbd, 0xe3, 0xc0, 0x01, 0x35, 0x2d, 0x54, 
    0x4f, 0x03
};

inline constexpr std::uint8_t kLayerBytes106[] = {
    0x03, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes107[] = {
    0x64, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes110[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes111[] = {
    0x20, 0x03, 0x00, 0x00, 0x58, 0x02, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes113[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes114[] = {
    0x02, 0x00, 0x00, 0x00, 0x2d, 0x33, 0x18, 0xa1, 0x00, 0xc2, 0x57, 0x07, 
    0xd9, 0x00
};

inline constexpr std::uint8_t kLayerBytes116[] = {
    0xa3, 0xaf, 0xa6, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes117[] = {
    0x02, 0x00, 0x00, 0x00, 0x1b, 0xa5, 0xd4, 0xe9, 0x01, 0xa2, 0x65, 0x2e, 
    0x0d, 0x03
};

inline constexpr std::uint8_t kLayerBytes119[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes120[] = {
    0x02, 0x00, 0x00, 0x00, 0xae, 0x90, 0xd8, 0x0d, 0x00, 0xb6, 0x4a, 0x36, 
    0x02, 0x00
};

inline constexpr std::uint8_t kLayerBytes122[] = {
    0x0a, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0xd0, 0x00, 
    0x00, 0x00, 0x90, 0x01, 0x00, 0x00, 0x5a, 0x00, 0x00, 0x00, 0x1c, 0x00, 
    0x00, 0x00, 0x8c, 0x13, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes125[] = {
    0x0e, 0x00, 0x00, 0x00, 0x57, 0x69, 0x6e, 0x64, 0x6f, 0x77, 0x20, 0x43, 
    0x61, 0x74, 0x61, 0x6c, 0x6f, 0x67
};

inline constexpr std::uint8_t kLayerBytes126[] = {
    0x00, 0x00, 0x00, 0x00, 0x02
};

inline constexpr std::uint8_t kLayerBytes127[] = {
    0x3a, 0x6e, 0x02, 0x39, 0x00, 0x01
};

inline constexpr std::uint8_t kLayerBytes129[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes130[] = {
    0x28, 0x4c, 0x8c, 0x93, 0x00, 0x01
};

inline constexpr std::uint8_t kLayerBytes132[] = {
    0x00, 0x00, 0x00, 0x00, 0x02
};

inline constexpr std::uint8_t kLayerBytes133[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes135[] = {
    0x06, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 
    0x00, 0x00, 0x90, 0x01, 0x00, 0x00, 0x5a, 0x00, 0x00, 0x00, 0x1c, 0x00, 
    0x00, 0x00, 0x8a, 0x13, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes138[] = {
    0x07, 0x00, 0x00, 0x00, 0x57, 0x69, 0x6e, 0x64, 0x6f, 0x77, 0x20
};

inline constexpr std::uint8_t kLayerBytes139[] = {
    0x00, 0x00, 0x00, 0x00, 0x02
};

inline constexpr std::uint8_t kLayerBytes140[] = {
    0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff
};

inline constexpr std::uint8_t kLayerBytes142[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes144[] = {
    0x17, 0x00, 0x00, 0x00, 0xd0, 0x9a, 0xd0, 0xb0, 0xd1, 0x82, 0xd0, 0xb0, 
    0xd0, 0xbb, 0xd0, 0xbe, 0xd0, 0xb3, 0x20, 0xd0, 0xbe, 0xd0, 0xba, 0xd0, 
    0xbe, 0xd0, 0xbd
};

inline constexpr std::uint8_t kLayerBytes145[] = {
    0xcb, 0xd5, 0x87, 0xe9, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes147[] = {
    0x00, 0x00, 0x00, 0x00, 0x02
};

inline constexpr std::uint8_t kLayerBytes148[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes150[] = {
    0x06, 0x00, 0x00, 0x00, 0x44, 0x65, 0x6c, 0x65, 0x74, 0x65
};

inline constexpr std::uint8_t kLayerBytes152[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes153[] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x04, 0x00, 0x00, 
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x80, 0xb4, 0x2e, 0xe9, 0x00, 
    0x28, 0x4c, 0x8c, 0x93, 0x00, 0x24, 0xb9, 0x8d, 0x6a, 0x00, 0xcb, 0xd5, 
    0x87, 0xe9, 0x00, 0x3a, 0x6e, 0x02, 0x39, 0x00, 0xc1, 0xa2, 0x97, 0x7c, 
    0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x0a, 
    0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 
    0x00, 0x00, 0xf7, 0x04, 0x64, 0xbd, 0x00, 0xa9, 0x6a, 0x93, 0xf9, 0x00, 
    0x3b, 0x0f, 0x93, 0x9a, 0x00, 0x31, 0x91, 0xf0, 0x2b, 0x00
};

inline constexpr std::uint8_t kLayerBytes154[] = {
    0x0f, 0x00, 0x00, 0x00, 0x03
};

inline constexpr std::uint8_t kLayerBytes155[] = {
    0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes157[] = {
    0x0e, 0x00, 0x00, 0x00, 0xd0, 0xa3, 0xd0, 0xb4, 0xd0, 0xb0, 0xd0, 0xbb, 
    0xd0, 0xb8, 0xd1, 0x82, 0xd1, 0x8c
};

inline constexpr std::uint8_t kLayerBytes159[] = {
    0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes160[] = {
    0xd3, 0x93, 0xd3, 0x4d, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes162[] = {
    0x60, 0x0a, 0x17, 0x31, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes163[] = {
    0x02, 0x00, 0x00, 0x00, 0x8d, 0xb9, 0xed, 0xda, 0x01, 0xc7, 0xae, 0x34, 
    0xe6, 0x03
};

inline constexpr std::uint8_t kLayerBytes165[] = {
    0x03, 0x00, 0x00, 0x00, 0x41, 0x64, 0x64
};

inline constexpr std::uint8_t kLayerBytes167[] = {
    0x08, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x6e, 0x00, 
    0x00, 0x00, 0x90, 0x01, 0x00, 0x00, 0x5a, 0x00, 0x00, 0x00, 0x1c, 0x00, 
    0x00, 0x00, 0x8b, 0x13, 0x00, 0x00
};

inline constexpr apptraverse::EmbeddedDistilledLayerRecord kLayers[] = {
    {1u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {1u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes1, sizeof(kLayerBytes1)},
    {1u, 3951442439u, static_cast<std::uint8_t>(0u), kLayerBytes2, sizeof(kLayerBytes2)},
    {1u, 3951442439u, static_cast<std::uint8_t>(1u), kLayerBytes3, sizeof(kLayerBytes3)},
    {1u, 3951442439u, static_cast<std::uint8_t>(2u), kLayerBytes4, sizeof(kLayerBytes4)},
    {2u, 1762613127u, static_cast<std::uint8_t>(0u), kLayerBytes5, sizeof(kLayerBytes5)},
    {2u, 1997769863u, static_cast<std::uint8_t>(0u), kLayerBytes6, sizeof(kLayerBytes6)},
    {2u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {2u, 2683386467u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {2u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes9, sizeof(kLayerBytes9)},
    {2u, 3503202128u, static_cast<std::uint8_t>(0u), kLayerBytes10, sizeof(kLayerBytes10)},
    {3u, 262198775u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3u, 3130269904u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3u, 3181634327u, static_cast<std::uint8_t>(0u), kLayerBytes14, sizeof(kLayerBytes14)},
    {3u, 4059871996u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {4u, 417285915u, static_cast<std::uint8_t>(0u), kLayerBytes16, sizeof(kLayerBytes16)},
    {4u, 679681973u, static_cast<std::uint8_t>(0u), kLayerBytes17, sizeof(kLayerBytes17)},
    {4u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {4u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes19, sizeof(kLayerBytes19)},
    {5u, 143530183u, static_cast<std::uint8_t>(0u), kLayerBytes20, sizeof(kLayerBytes20)},
    {5u, 156022664u, static_cast<std::uint8_t>(0u), kLayerBytes21, sizeof(kLayerBytes21)},
    {5u, 262198775u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {5u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {6u, 1491514641u, static_cast<std::uint8_t>(0u), kLayerBytes24, sizeof(kLayerBytes24)},
    {6u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {6u, 3081381164u, static_cast<std::uint8_t>(0u), kLayerBytes26, sizeof(kLayerBytes26)},
    {6u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes27, sizeof(kLayerBytes27)},
    {8u, 1491514641u, static_cast<std::uint8_t>(0u), kLayerBytes28, sizeof(kLayerBytes28)},
    {8u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {8u, 2655033684u, static_cast<std::uint8_t>(0u), kLayerBytes30, sizeof(kLayerBytes30)},
    {8u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes31, sizeof(kLayerBytes31)},
    {10u, 1491514641u, static_cast<std::uint8_t>(0u), kLayerBytes32, sizeof(kLayerBytes32)},
    {10u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {10u, 2458817115u, static_cast<std::uint8_t>(0u), kLayerBytes34, sizeof(kLayerBytes34)},
    {10u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes35, sizeof(kLayerBytes35)},
    {12u, 1427799366u, static_cast<std::uint8_t>(0u), kLayerBytes36, sizeof(kLayerBytes36)},
    {12u, 1491514641u, static_cast<std::uint8_t>(0u), kLayerBytes37, sizeof(kLayerBytes37)},
    {12u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {12u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes39, sizeof(kLayerBytes39)},
    {15u, 1762613127u, static_cast<std::uint8_t>(0u), kLayerBytes40, sizeof(kLayerBytes40)},
    {15u, 1997769863u, static_cast<std::uint8_t>(0u), kLayerBytes41, sizeof(kLayerBytes41)},
    {15u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {15u, 2683386467u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {15u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes44, sizeof(kLayerBytes44)},
    {15u, 3503202128u, static_cast<std::uint8_t>(0u), kLayerBytes45, sizeof(kLayerBytes45)},
    {16u, 262198775u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {16u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {16u, 3130269904u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {16u, 3181634327u, static_cast<std::uint8_t>(0u), kLayerBytes49, sizeof(kLayerBytes49)},
    {16u, 4059871996u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {37112502u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {37112502u, 3314865188u, static_cast<std::uint8_t>(0u), kLayerBytes52, sizeof(kLayerBytes52)},
    {121310458u, 438600091u, static_cast<std::uint8_t>(0u), kLayerBytes53, sizeof(kLayerBytes53)},
    {121310458u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {121310458u, 2738482936u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {195473315u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {195473315u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes57, sizeof(kLayerBytes57)},
    {195473315u, 3527371339u, static_cast<std::uint8_t>(0u), kLayerBytes58, sizeof(kLayerBytes58)},
    {221144482u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {221144482u, 3314865188u, static_cast<std::uint8_t>(0u), kLayerBytes60, sizeof(kLayerBytes60)},
    {232296622u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {232296622u, 3314865188u, static_cast<std::uint8_t>(0u), kLayerBytes62, sizeof(kLayerBytes62)},
    {737186097u, 262198775u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {737186097u, 1588818980u, static_cast<std::uint8_t>(0u), kLayerBytes64, sizeof(kLayerBytes64)},
    {737186097u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {749788663u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {749788663u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes67, sizeof(kLayerBytes67)},
    {749788663u, 3527371339u, static_cast<std::uint8_t>(0u), kLayerBytes68, sizeof(kLayerBytes68)},
    {799741041u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {799741041u, 3314865188u, static_cast<std::uint8_t>(0u), kLayerBytes70, sizeof(kLayerBytes70)},
    {823593568u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {823593568u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes72, sizeof(kLayerBytes72)},
    {823593568u, 3527371339u, static_cast<std::uint8_t>(0u), kLayerBytes73, sizeof(kLayerBytes73)},
    {956460602u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {956460602u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes75, sizeof(kLayerBytes75)},
    {956460602u, 3527371339u, static_cast<std::uint8_t>(0u), kLayerBytes76, sizeof(kLayerBytes76)},
    {1159150112u, 1762613127u, static_cast<std::uint8_t>(0u), kLayerBytes77, sizeof(kLayerBytes77)},
    {1159150112u, 1997769863u, static_cast<std::uint8_t>(0u), kLayerBytes78, sizeof(kLayerBytes78)},
    {1159150112u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {1159150112u, 2683386467u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {1159150112u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes81, sizeof(kLayerBytes81)},
    {1159150112u, 3503202128u, static_cast<std::uint8_t>(0u), kLayerBytes82, sizeof(kLayerBytes82)},
    {1305711571u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {1305711571u, 2553695344u, static_cast<std::uint8_t>(0u), kLayerBytes84, sizeof(kLayerBytes84)},
    {1305711571u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes85, sizeof(kLayerBytes85)},
    {1330916661u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {1330916661u, 3314865188u, static_cast<std::uint8_t>(0u), kLayerBytes87, sizeof(kLayerBytes87)},
    {1368518031u, 1491514641u, static_cast<std::uint8_t>(0u), kLayerBytes88, sizeof(kLayerBytes88)},
    {1368518031u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {1368518031u, 2655033684u, static_cast<std::uint8_t>(0u), kLayerBytes90, sizeof(kLayerBytes90)},
    {1368518031u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes91, sizeof(kLayerBytes91)},
    {1745958460u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {1745958460u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes93, sizeof(kLayerBytes93)},
    {1745958460u, 3527371339u, static_cast<std::uint8_t>(0u), kLayerBytes94, sizeof(kLayerBytes94)},
    {1787672868u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {1787672868u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes96, sizeof(kLayerBytes96)},
    {1787672868u, 3527371339u, static_cast<std::uint8_t>(0u), kLayerBytes97, sizeof(kLayerBytes97)},
    {1811373555u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {1811373555u, 3314865188u, static_cast<std::uint8_t>(0u), kLayerBytes99, sizeof(kLayerBytes99)},
    {2025751932u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {2025751932u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes101, sizeof(kLayerBytes101)},
    {2025751932u, 3527371339u, static_cast<std::uint8_t>(0u), kLayerBytes102, sizeof(kLayerBytes102)},
    {2090312385u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {2090312385u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes104, sizeof(kLayerBytes104)},
    {2090312385u, 3527371339u, static_cast<std::uint8_t>(0u), kLayerBytes105, sizeof(kLayerBytes105)},
    {2159371811u, 1762613127u, static_cast<std::uint8_t>(0u), kLayerBytes106, sizeof(kLayerBytes106)},
    {2159371811u, 1997769863u, static_cast<std::uint8_t>(0u), kLayerBytes107, sizeof(kLayerBytes107)},
    {2159371811u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {2159371811u, 2683386467u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {2159371811u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes110, sizeof(kLayerBytes110)},
    {2159371811u, 3503202128u, static_cast<std::uint8_t>(0u), kLayerBytes111, sizeof(kLayerBytes111)},
    {2291516309u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {2291516309u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes113, sizeof(kLayerBytes113)},
    {2291516309u, 3527371339u, static_cast<std::uint8_t>(0u), kLayerBytes114, sizeof(kLayerBytes114)},
    {2475445288u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {2475445288u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes116, sizeof(kLayerBytes116)},
    {2475445288u, 3527371339u, static_cast<std::uint8_t>(0u), kLayerBytes117, sizeof(kLayerBytes117)},
    {2535125362u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {2535125362u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes119, sizeof(kLayerBytes119)},
    {2535125362u, 3527371339u, static_cast<std::uint8_t>(0u), kLayerBytes120, sizeof(kLayerBytes120)},
    {2593328955u, 262198775u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {2593328955u, 1588818980u, static_cast<std::uint8_t>(0u), kLayerBytes122, sizeof(kLayerBytes122)},
    {2593328955u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {2702717741u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {2702717741u, 3314865188u, static_cast<std::uint8_t>(0u), kLayerBytes125, sizeof(kLayerBytes125)},
    {3074351488u, 1427799366u, static_cast<std::uint8_t>(0u), kLayerBytes126, sizeof(kLayerBytes126)},
    {3074351488u, 1491514641u, static_cast<std::uint8_t>(0u), kLayerBytes127, sizeof(kLayerBytes127)},
    {3074351488u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3074351488u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes129, sizeof(kLayerBytes129)},
    {3146126689u, 1491514641u, static_cast<std::uint8_t>(0u), kLayerBytes130, sizeof(kLayerBytes130)},
    {3146126689u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3146126689u, 3081381164u, static_cast<std::uint8_t>(0u), kLayerBytes132, sizeof(kLayerBytes132)},
    {3146126689u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes133, sizeof(kLayerBytes133)},
    {3177448695u, 262198775u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3177448695u, 1588818980u, static_cast<std::uint8_t>(0u), kLayerBytes135, sizeof(kLayerBytes135)},
    {3177448695u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3236150605u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3236150605u, 3314865188u, static_cast<std::uint8_t>(0u), kLayerBytes138, sizeof(kLayerBytes138)},
    {3397290007u, 417285915u, static_cast<std::uint8_t>(0u), kLayerBytes139, sizeof(kLayerBytes139)},
    {3397290007u, 679681973u, static_cast<std::uint8_t>(0u), kLayerBytes140, sizeof(kLayerBytes140)},
    {3397290007u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3397290007u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes142, sizeof(kLayerBytes142)},
    {3641137090u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3641137090u, 3314865188u, static_cast<std::uint8_t>(0u), kLayerBytes144, sizeof(kLayerBytes144)},
    {3644373215u, 1491514641u, static_cast<std::uint8_t>(0u), kLayerBytes145, sizeof(kLayerBytes145)},
    {3644373215u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3644373215u, 2458817115u, static_cast<std::uint8_t>(0u), kLayerBytes147, sizeof(kLayerBytes147)},
    {3644373215u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes148, sizeof(kLayerBytes148)},
    {3673012621u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3673012621u, 3314865188u, static_cast<std::uint8_t>(0u), kLayerBytes150, sizeof(kLayerBytes150)},
    {3683371484u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3683371484u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes152, sizeof(kLayerBytes152)},
    {3683371484u, 3951442439u, static_cast<std::uint8_t>(0u), kLayerBytes153, sizeof(kLayerBytes153)},
    {3683371484u, 3951442439u, static_cast<std::uint8_t>(1u), kLayerBytes154, sizeof(kLayerBytes154)},
    {3683371484u, 3951442439u, static_cast<std::uint8_t>(2u), kLayerBytes155, sizeof(kLayerBytes155)},
    {3862212295u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3862212295u, 3314865188u, static_cast<std::uint8_t>(0u), kLayerBytes157, sizeof(kLayerBytes157)},
    {3912152192u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3912152192u, 2553695344u, static_cast<std::uint8_t>(0u), kLayerBytes159, sizeof(kLayerBytes159)},
    {3912152192u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes160, sizeof(kLayerBytes160)},
    {3917993419u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3917993419u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes162, sizeof(kLayerBytes162)},
    {3917993419u, 3527371339u, static_cast<std::uint8_t>(0u), kLayerBytes163, sizeof(kLayerBytes163)},
    {3923027227u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3923027227u, 3314865188u, static_cast<std::uint8_t>(0u), kLayerBytes165, sizeof(kLayerBytes165)},
    {4187187881u, 262198775u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {4187187881u, 1588818980u, static_cast<std::uint8_t>(0u), kLayerBytes167, sizeof(kLayerBytes167)},
    {4187187881u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
};

inline constexpr apptraverse::EmbeddedDistilledSupply kSupplyConstants = {nullptr, 0};

inline constexpr apptraverse::EmbeddedDistilledGraph kEmbeddedGraph = {1u, kSupplyConstants, kLayers, 169};

}  // namespace window_catalog
}  // namespace embedded_distilled
}  // namespace apptraverse

#endif  // APPTRAVERSE_EMBEDDED_DISTILLED_WINDOW_CATALOG_SUPPLY_H_
