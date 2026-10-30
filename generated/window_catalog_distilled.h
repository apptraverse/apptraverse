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
    0xdb, 0x19, 0x3f, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes2[] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x04, 0x00, 0x00, 
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x3a, 0xcb, 0xdb, 0x4d, 0x00, 
    0x42, 0x2b, 0x02, 0x80, 0x00, 0xd9, 0xd7, 0xbc, 0xc0, 0x00, 0x35, 0x0e, 
    0x84, 0xa8, 0x00, 0x2c, 0xd9, 0xff, 0x64, 0x00, 0xd4, 0x9e, 0xf2, 0x96, 
    0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x0a, 
    0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 
    0x00, 0x00, 0xca, 0x00, 0xc3, 0xca, 0x00, 0xf5, 0x44, 0x8c, 0xfc, 0x00, 
    0x5a, 0x21, 0xff, 0x75, 0x00, 0xd5, 0x8e, 0x93, 0x30, 0x00, 0x0f, 0x00, 
    0x00, 0x00, 0x03, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes3[] = {
    0x03, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes4[] = {
    0x50, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes7[] = {
    0x53, 0x87, 0xa7, 0xdc, 0x00, 0x01, 0x00, 0x00, 0x00, 0x25, 0xd6, 0x16, 
    0xd3, 0x00, 0xa7, 0x39, 0x37, 0x0f, 0x04, 0x5f, 0x06, 0x00, 0xa7, 0x39, 
    0x37, 0x0f, 0x04, 0x5f, 0x06, 0x00
};

inline constexpr std::uint8_t kLayerBytes8[] = {
    0x80, 0x02, 0x00, 0x00, 0xe0, 0x01, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes12[] = {
    0x02, 0x00, 0x00, 0x00, 0x00, 0x30, 0xb4, 0xc4, 0x24, 0x00, 0x3a, 0xcb, 
    0xdb, 0x4d, 0x00
};

inline constexpr std::uint8_t kLayerBytes14[] = {
    0x01, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes15[] = {
    0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff
};

inline constexpr std::uint8_t kLayerBytes17[] = {
    0xc3, 0x5d, 0x0c, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes18[] = {
    0x01, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes19[] = {
    0x04, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 
    0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x90, 0x01, 0x00, 0x00, 0x7c, 0x01, 
    0x00, 0x00, 0x89, 0x13, 0x00, 0x00, 0x01
};

inline constexpr std::uint8_t kLayerBytes22[] = {
    0x42, 0x2b, 0x02, 0x80, 0x00, 0x01
};

inline constexpr std::uint8_t kLayerBytes24[] = {
    0x01, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes25[] = {
    0x27, 0x8c, 0xe8, 0xe3, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes26[] = {
    0xd9, 0xd7, 0xbc, 0xc0, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes28[] = {
    0x01, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes29[] = {
    0x5c, 0xf3, 0x0f, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes30[] = {
    0x35, 0x0e, 0x84, 0xa8, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes32[] = {
    0x01, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes33[] = {
    0xf9, 0x60, 0x69, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes34[] = {
    0x01, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes35[] = {
    0x2c, 0xd9, 0xff, 0x64, 0x00, 0x01
};

inline constexpr std::uint8_t kLayerBytes37[] = {
    0xf2, 0xfb, 0xb4, 0xf3, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes38[] = {
    0x10, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes39[] = {
    0xa0, 0x00, 0x00, 0x00, 0x78, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes42[] = {
    0x4b, 0xa0, 0x0c, 0xf5, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes43[] = {
    0xe0, 0x01, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes47[] = {
    0x0f, 0x00, 0x00, 0x00, 0x00, 0xd4, 0x9e, 0xf2, 0x96, 0x00, 0x3a, 0xcb, 
    0xdb, 0x4d, 0x00
};

inline constexpr std::uint8_t kLayerBytes50[] = {
    0x02, 0x00, 0x00, 0x00, 0x45, 0x4e
};

inline constexpr std::uint8_t kLayerBytes52[] = {
    0x0e, 0x00, 0x00, 0x00, 0x57, 0x69, 0x6e, 0x64, 0x6f, 0x77, 0x20, 0x43, 
    0x61, 0x74, 0x61, 0x6c, 0x6f, 0x67
};

inline constexpr std::uint8_t kLayerBytes53[] = {
    0x00, 0x00, 0x00, 0x00, 0x02
};

inline constexpr std::uint8_t kLayerBytes54[] = {
    0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff
};

inline constexpr std::uint8_t kLayerBytes56[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes57[] = {
    0x35, 0x0e, 0x84, 0xa8, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes59[] = {
    0x00, 0x00, 0x00, 0x00, 0x02
};

inline constexpr std::uint8_t kLayerBytes60[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes62[] = {
    0xf2, 0x73, 0x10, 0xa4, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes63[] = {
    0x02, 0x00, 0x00, 0x00, 0x01, 0xe6, 0xd3, 0x0c, 0x01, 0x95, 0x9e, 0xaa, 
    0x3c, 0x03
};

inline constexpr std::uint8_t kLayerBytes65[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes66[] = {
    0x02, 0x00, 0x00, 0x00, 0xa4, 0x06, 0xc6, 0x34, 0x00, 0x06, 0xfc, 0x30, 
    0x53, 0x00
};

inline constexpr std::uint8_t kLayerBytes68[] = {
    0x0c, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x32, 0x01, 
    0x00, 0x00, 0x90, 0x01, 0x00, 0x00, 0x5a, 0x00, 0x00, 0x00, 0x1c, 0x00, 
    0x00, 0x00, 0x8d, 0x13, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes71[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes72[] = {
    0x02, 0x00, 0x00, 0x00, 0xf0, 0x44, 0x3b, 0x48, 0x00, 0x30, 0x92, 0x7a, 
    0x50, 0x00
};

inline constexpr std::uint8_t kLayerBytes74[] = {
    0x04, 0x00, 0x00, 0x00, 0x4f, 0x70, 0x65, 0x6e
};

inline constexpr std::uint8_t kLayerBytes75[] = {
    0xd9, 0xd7, 0xbc, 0xc0, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes77[] = {
    0x00, 0x00, 0x00, 0x00, 0x02
};

inline constexpr std::uint8_t kLayerBytes78[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes80[] = {
    0x17, 0x00, 0x00, 0x00, 0xd0, 0x9a, 0xd0, 0xb0, 0xd1, 0x82, 0xd0, 0xb0, 
    0xd0, 0xbb, 0xd0, 0xbe, 0xd0, 0xb3, 0x20, 0xd0, 0xbe, 0xd0, 0xba, 0xd0, 
    0xbe, 0xd0, 0xbd
};

inline constexpr std::uint8_t kLayerBytes82[] = {
    0x07, 0x00, 0x00, 0x00, 0x57, 0x69, 0x6e, 0x64, 0x6f, 0x77, 0x20
};

inline constexpr std::uint8_t kLayerBytes84[] = {
    0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes85[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes87[] = {
    0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes88[] = {
    0xe2, 0x5f, 0x48, 0x4c, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes90[] = {
    0x09, 0x00, 0x00, 0x00, 0xd0, 0x9e, 0xd0, 0xba, 0xd0, 0xbd, 0xd0, 0xbe, 
    0x20
};

inline constexpr std::uint8_t kLayerBytes92[] = {
    0x0e, 0x00, 0x00, 0x00, 0xd0, 0x9e, 0xd1, 0x82, 0xd0, 0xba, 0xd1, 0x80, 
    0xd1, 0x8b, 0xd1, 0x82, 0xd1, 0x8c
};

inline constexpr std::uint8_t kLayerBytes94[] = {
    0x47, 0x49, 0xdd, 0x68, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes95[] = {
    0x02, 0x00, 0x00, 0x00, 0x22, 0xec, 0x6c, 0xdd, 0x01, 0x00, 0x88, 0x3c, 
    0x0c, 0x03
};

inline constexpr std::uint8_t kLayerBytes97[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes98[] = {
    0x02, 0x00, 0x00, 0x00, 0x22, 0xec, 0x6c, 0xdd, 0x00, 0x00, 0x88, 0x3c, 
    0x0c, 0x00
};

inline constexpr std::uint8_t kLayerBytes100[] = {
    0x06, 0x00, 0x00, 0x00, 0x44, 0x65, 0x6c, 0x65, 0x74, 0x65
};

inline constexpr std::uint8_t kLayerBytes102[] = {
    0x0a, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0xd0, 0x00, 
    0x00, 0x00, 0x90, 0x01, 0x00, 0x00, 0x5a, 0x00, 0x00, 0x00, 0x1c, 0x00, 
    0x00, 0x00, 0x8c, 0x13, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes105[] = {
    0xeb, 0x1b, 0x1d, 0xe5, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes106[] = {
    0x02, 0x00, 0x00, 0x00, 0x3e, 0x6e, 0xcc, 0xce, 0x01, 0x91, 0x2b, 0x9f, 
    0xf9, 0x03
};

inline constexpr std::uint8_t kLayerBytes108[] = {
    0x0e, 0x00, 0x00, 0x00, 0xd0, 0xa3, 0xd0, 0xb4, 0xd0, 0xb0, 0xd0, 0xbb, 
    0xd0, 0xb8, 0xd1, 0x82, 0xd1, 0x8c
};

inline constexpr std::uint8_t kLayerBytes110[] = {
    0x55, 0x4d, 0xa9, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes111[] = {
    0x02, 0x00, 0x00, 0x00, 0xf0, 0x44, 0x3b, 0x48, 0x01, 0x30, 0x92, 0x7a, 
    0x50, 0x03
};

inline constexpr std::uint8_t kLayerBytes113[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes114[] = {
    0x02, 0x00, 0x00, 0x00, 0x01, 0xe6, 0xd3, 0x0c, 0x00, 0x95, 0x9e, 0xaa, 
    0x3c, 0x00
};

inline constexpr std::uint8_t kLayerBytes116[] = {
    0x33, 0x2f, 0xb2, 0xec, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes117[] = {
    0x02, 0x00, 0x00, 0x00, 0xa3, 0x4c, 0x81, 0x69, 0x01, 0x0f, 0xf0, 0x4e, 
    0x96, 0x03
};

inline constexpr std::uint8_t kLayerBytes119[] = {
    0x95, 0x04, 0x32, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes120[] = {
    0x02, 0x00, 0x00, 0x00, 0xa4, 0x06, 0xc6, 0x34, 0x01, 0x06, 0xfc, 0x30, 
    0x53, 0x03
};

inline constexpr std::uint8_t kLayerBytes122[] = {
    0x06, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 
    0x00, 0x00, 0x90, 0x01, 0x00, 0x00, 0x5a, 0x00, 0x00, 0x00, 0x1c, 0x00, 
    0x00, 0x00, 0x8a, 0x13, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes125[] = {
    0x03, 0x00, 0x00, 0x00, 0x41, 0x64, 0x64
};

inline constexpr std::uint8_t kLayerBytes126[] = {
    0x50, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x80, 0x02, 0x00, 0x00, 
    0xe0, 0x01, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes129[] = {
    0x03, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes130[] = {
    0x64, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes133[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes134[] = {
    0x20, 0x03, 0x00, 0x00, 0x58, 0x02, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes136[] = {
    0x02, 0x00, 0x00, 0x00, 0x52, 0x55
};

inline constexpr std::uint8_t kLayerBytes137[] = {
    0x42, 0x2b, 0x02, 0x80, 0x00, 0x01
};

inline constexpr std::uint8_t kLayerBytes139[] = {
    0x00, 0x00, 0x00, 0x00, 0x02
};

inline constexpr std::uint8_t kLayerBytes140[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes142[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes143[] = {
    0x02, 0x00, 0x00, 0x00, 0x3e, 0x6e, 0xcc, 0xce, 0x00, 0x91, 0x2b, 0x9f, 
    0xf9, 0x00
};

inline constexpr std::uint8_t kLayerBytes145[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes146[] = {
    0x02, 0x00, 0x00, 0x00, 0xa3, 0x4c, 0x81, 0x69, 0x00, 0x0f, 0xf0, 0x4e, 
    0x96, 0x00
};

inline constexpr std::uint8_t kLayerBytes147[] = {
    0x00, 0x00, 0x00, 0x00, 0x02
};

inline constexpr std::uint8_t kLayerBytes148[] = {
    0x2c, 0xd9, 0xff, 0x64, 0x00, 0x01
};

inline constexpr std::uint8_t kLayerBytes150[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes151[] = {
    0x10, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes152[] = {
    0xa0, 0x00, 0x00, 0x00, 0x78, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes155[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes156[] = {
    0xe0, 0x01, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes158[] = {
    0x10, 0x00, 0x00, 0x00, 0xd0, 0x94, 0xd0, 0xbe, 0xd0, 0xb1, 0xd0, 0xb0, 
    0xd0, 0xb2, 0xd0, 0xb8, 0xd1, 0x82, 0xd1, 0x8c
};

inline constexpr std::uint8_t kLayerBytes160[] = {
    0x08, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x6e, 0x00, 
    0x00, 0x00, 0x90, 0x01, 0x00, 0x00, 0x5a, 0x00, 0x00, 0x00, 0x1c, 0x00, 
    0x00, 0x00, 0x8b, 0x13, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes163[] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kLayerBytes164[] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x04, 0x00, 0x00, 
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x3a, 0xcb, 0xdb, 0x4d, 0x00, 
    0x42, 0x2b, 0x02, 0x80, 0x00, 0xd9, 0xd7, 0xbc, 0xc0, 0x00, 0x35, 0x0e, 
    0x84, 0xa8, 0x00, 0x2c, 0xd9, 0xff, 0x64, 0x00, 0xd4, 0x9e, 0xf2, 0x96, 
    0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x0a, 
    0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 
    0x00, 0x00, 0xca, 0x00, 0xc3, 0xca, 0x00, 0xf5, 0x44, 0x8c, 0xfc, 0x00, 
    0x5a, 0x21, 0xff, 0x75, 0x00, 0xd5, 0x8e, 0x93, 0x30, 0x00, 0x0f, 0x00, 
    0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00
};

inline constexpr ae::ObjId::Type kSupplyConstantIds[] = {15, 16};

inline constexpr apptraverse::EmbeddedDistilledLayerRecord kLayers[] = {
    {1u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {1u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes1, sizeof(kLayerBytes1)},
    {1u, 3951442439u, static_cast<std::uint8_t>(0u), kLayerBytes2, sizeof(kLayerBytes2)},
    {2u, 1762613127u, static_cast<std::uint8_t>(0u), kLayerBytes3, sizeof(kLayerBytes3)},
    {2u, 1997769863u, static_cast<std::uint8_t>(0u), kLayerBytes4, sizeof(kLayerBytes4)},
    {2u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {2u, 2683386467u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {2u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes7, sizeof(kLayerBytes7)},
    {2u, 3503202128u, static_cast<std::uint8_t>(0u), kLayerBytes8, sizeof(kLayerBytes8)},
    {3u, 262198775u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3u, 3130269904u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3u, 3181634327u, static_cast<std::uint8_t>(0u), kLayerBytes12, sizeof(kLayerBytes12)},
    {3u, 4059871996u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {4u, 417285915u, static_cast<std::uint8_t>(0u), kLayerBytes14, sizeof(kLayerBytes14)},
    {4u, 679681973u, static_cast<std::uint8_t>(0u), kLayerBytes15, sizeof(kLayerBytes15)},
    {4u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {4u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes17, sizeof(kLayerBytes17)},
    {5u, 143530183u, static_cast<std::uint8_t>(0u), kLayerBytes18, sizeof(kLayerBytes18)},
    {5u, 156022664u, static_cast<std::uint8_t>(0u), kLayerBytes19, sizeof(kLayerBytes19)},
    {5u, 262198775u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {5u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {6u, 1491514641u, static_cast<std::uint8_t>(0u), kLayerBytes22, sizeof(kLayerBytes22)},
    {6u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {6u, 3081381164u, static_cast<std::uint8_t>(0u), kLayerBytes24, sizeof(kLayerBytes24)},
    {6u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes25, sizeof(kLayerBytes25)},
    {8u, 1491514641u, static_cast<std::uint8_t>(0u), kLayerBytes26, sizeof(kLayerBytes26)},
    {8u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {8u, 2655033684u, static_cast<std::uint8_t>(0u), kLayerBytes28, sizeof(kLayerBytes28)},
    {8u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes29, sizeof(kLayerBytes29)},
    {10u, 1491514641u, static_cast<std::uint8_t>(0u), kLayerBytes30, sizeof(kLayerBytes30)},
    {10u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {10u, 2458817115u, static_cast<std::uint8_t>(0u), kLayerBytes32, sizeof(kLayerBytes32)},
    {10u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes33, sizeof(kLayerBytes33)},
    {12u, 1427799366u, static_cast<std::uint8_t>(0u), kLayerBytes34, sizeof(kLayerBytes34)},
    {12u, 1491514641u, static_cast<std::uint8_t>(0u), kLayerBytes35, sizeof(kLayerBytes35)},
    {12u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {12u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes37, sizeof(kLayerBytes37)},
    {15u, 1762613127u, static_cast<std::uint8_t>(0u), kLayerBytes38, sizeof(kLayerBytes38)},
    {15u, 1997769863u, static_cast<std::uint8_t>(0u), kLayerBytes39, sizeof(kLayerBytes39)},
    {15u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {15u, 2683386467u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {15u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes42, sizeof(kLayerBytes42)},
    {15u, 3503202128u, static_cast<std::uint8_t>(0u), kLayerBytes43, sizeof(kLayerBytes43)},
    {16u, 262198775u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {16u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {16u, 3130269904u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {16u, 3181634327u, static_cast<std::uint8_t>(0u), kLayerBytes47, sizeof(kLayerBytes47)},
    {16u, 4059871996u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {205293568u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {205293568u, 3314865188u, static_cast<std::uint8_t>(0u), kLayerBytes50, sizeof(kLayerBytes50)},
    {215213569u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {215213569u, 3314865188u, static_cast<std::uint8_t>(0u), kLayerBytes52, sizeof(kLayerBytes52)},
    {319577539u, 417285915u, static_cast<std::uint8_t>(0u), kLayerBytes53, sizeof(kLayerBytes53)},
    {319577539u, 679681973u, static_cast<std::uint8_t>(0u), kLayerBytes54, sizeof(kLayerBytes54)},
    {319577539u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {319577539u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes56, sizeof(kLayerBytes56)},
    {476668153u, 1491514641u, static_cast<std::uint8_t>(0u), kLayerBytes57, sizeof(kLayerBytes57)},
    {476668153u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {476668153u, 2458817115u, static_cast<std::uint8_t>(0u), kLayerBytes59, sizeof(kLayerBytes59)},
    {476668153u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes60, sizeof(kLayerBytes60)},
    {616870960u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {616870960u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes62, sizeof(kLayerBytes62)},
    {616870960u, 3527371339u, static_cast<std::uint8_t>(0u), kLayerBytes63, sizeof(kLayerBytes63)},
    {808584341u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {808584341u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes65, sizeof(kLayerBytes65)},
    {808584341u, 3527371339u, static_cast<std::uint8_t>(0u), kLayerBytes66, sizeof(kLayerBytes66)},
    {814976725u, 262198775u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {814976725u, 1588818980u, static_cast<std::uint8_t>(0u), kLayerBytes68, sizeof(kLayerBytes68)},
    {814976725u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {816401749u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {816401749u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes71, sizeof(kLayerBytes71)},
    {816401749u, 3527371339u, static_cast<std::uint8_t>(0u), kLayerBytes72, sizeof(kLayerBytes72)},
    {885393060u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {885393060u, 3314865188u, static_cast<std::uint8_t>(0u), kLayerBytes74, sizeof(kLayerBytes74)},
    {1007678300u, 1491514641u, static_cast<std::uint8_t>(0u), kLayerBytes75, sizeof(kLayerBytes75)},
    {1007678300u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {1007678300u, 2655033684u, static_cast<std::uint8_t>(0u), kLayerBytes77, sizeof(kLayerBytes77)},
    {1007678300u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes78, sizeof(kLayerBytes78)},
    {1017814677u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {1017814677u, 3314865188u, static_cast<std::uint8_t>(0u), kLayerBytes80, sizeof(kLayerBytes80)},
    {1211843824u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {1211843824u, 3314865188u, static_cast<std::uint8_t>(0u), kLayerBytes82, sizeof(kLayerBytes82)},
    {1279811554u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {1279811554u, 2553695344u, static_cast<std::uint8_t>(0u), kLayerBytes84, sizeof(kLayerBytes84)},
    {1279811554u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes85, sizeof(kLayerBytes85)},
    {1306250042u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {1306250042u, 2553695344u, static_cast<std::uint8_t>(0u), kLayerBytes87, sizeof(kLayerBytes87)},
    {1306250042u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes88, sizeof(kLayerBytes88)},
    {1350210096u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {1350210096u, 3314865188u, static_cast<std::uint8_t>(0u), kLayerBytes90, sizeof(kLayerBytes90)},
    {1395719174u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {1395719174u, 3314865188u, static_cast<std::uint8_t>(0u), kLayerBytes92, sizeof(kLayerBytes92)},
    {1694488876u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {1694488876u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes94, sizeof(kLayerBytes94)},
    {1694488876u, 3527371339u, static_cast<std::uint8_t>(0u), kLayerBytes95, sizeof(kLayerBytes95)},
    {1759332679u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {1759332679u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes97, sizeof(kLayerBytes97)},
    {1759332679u, 3527371339u, static_cast<std::uint8_t>(0u), kLayerBytes98, sizeof(kLayerBytes98)},
    {1770081443u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {1770081443u, 3314865188u, static_cast<std::uint8_t>(0u), kLayerBytes100, sizeof(kLayerBytes100)},
    {1979654490u, 262198775u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {1979654490u, 1588818980u, static_cast<std::uint8_t>(0u), kLayerBytes102, sizeof(kLayerBytes102)},
    {1979654490u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {2147625794u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {2147625794u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes105, sizeof(kLayerBytes105)},
    {2147625794u, 3527371339u, static_cast<std::uint8_t>(0u), kLayerBytes106, sizeof(kLayerBytes106)},
    {2521755663u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {2521755663u, 3314865188u, static_cast<std::uint8_t>(0u), kLayerBytes108, sizeof(kLayerBytes108)},
    {2532482772u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {2532482772u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes110, sizeof(kLayerBytes110)},
    {2532482772u, 3527371339u, static_cast<std::uint8_t>(0u), kLayerBytes111, sizeof(kLayerBytes111)},
    {2752541682u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {2752541682u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes113, sizeof(kLayerBytes113)},
    {2752541682u, 3527371339u, static_cast<std::uint8_t>(0u), kLayerBytes114, sizeof(kLayerBytes114)},
    {2827226677u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {2827226677u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes116, sizeof(kLayerBytes116)},
    {2827226677u, 3527371339u, static_cast<std::uint8_t>(0u), kLayerBytes117, sizeof(kLayerBytes117)},
    {3233601497u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3233601497u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes119, sizeof(kLayerBytes119)},
    {3233601497u, 3527371339u, static_cast<std::uint8_t>(0u), kLayerBytes120, sizeof(kLayerBytes120)},
    {3401777354u, 262198775u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3401777354u, 1588818980u, static_cast<std::uint8_t>(0u), kLayerBytes122, sizeof(kLayerBytes122)},
    {3401777354u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3469504062u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3469504062u, 3314865188u, static_cast<std::uint8_t>(0u), kLayerBytes125, sizeof(kLayerBytes125)},
    {3541489189u, 438600091u, static_cast<std::uint8_t>(0u), kLayerBytes126, sizeof(kLayerBytes126)},
    {3541489189u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3541489189u, 2738482936u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3701966675u, 1762613127u, static_cast<std::uint8_t>(0u), kLayerBytes129, sizeof(kLayerBytes129)},
    {3701966675u, 1997769863u, static_cast<std::uint8_t>(0u), kLayerBytes130, sizeof(kLayerBytes130)},
    {3701966675u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3701966675u, 2683386467u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3701966675u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes133, sizeof(kLayerBytes133)},
    {3701966675u, 3503202128u, static_cast<std::uint8_t>(0u), kLayerBytes134, sizeof(kLayerBytes134)},
    {3714903074u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3714903074u, 3314865188u, static_cast<std::uint8_t>(0u), kLayerBytes136, sizeof(kLayerBytes136)},
    {3823668263u, 1491514641u, static_cast<std::uint8_t>(0u), kLayerBytes137, sizeof(kLayerBytes137)},
    {3823668263u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3823668263u, 3081381164u, static_cast<std::uint8_t>(0u), kLayerBytes139, sizeof(kLayerBytes139)},
    {3823668263u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes140, sizeof(kLayerBytes140)},
    {3843890155u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3843890155u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes142, sizeof(kLayerBytes142)},
    {3843890155u, 3527371339u, static_cast<std::uint8_t>(0u), kLayerBytes143, sizeof(kLayerBytes143)},
    {3971100467u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {3971100467u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes145, sizeof(kLayerBytes145)},
    {3971100467u, 3527371339u, static_cast<std::uint8_t>(0u), kLayerBytes146, sizeof(kLayerBytes146)},
    {4088724466u, 1427799366u, static_cast<std::uint8_t>(0u), kLayerBytes147, sizeof(kLayerBytes147)},
    {4088724466u, 1491514641u, static_cast<std::uint8_t>(0u), kLayerBytes148, sizeof(kLayerBytes148)},
    {4088724466u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {4088724466u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes150, sizeof(kLayerBytes150)},
    {4111245387u, 1762613127u, static_cast<std::uint8_t>(0u), kLayerBytes151, sizeof(kLayerBytes151)},
    {4111245387u, 1997769863u, static_cast<std::uint8_t>(0u), kLayerBytes152, sizeof(kLayerBytes152)},
    {4111245387u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {4111245387u, 2683386467u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {4111245387u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes155, sizeof(kLayerBytes155)},
    {4111245387u, 3503202128u, static_cast<std::uint8_t>(0u), kLayerBytes156, sizeof(kLayerBytes156)},
    {4187958161u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {4187958161u, 3314865188u, static_cast<std::uint8_t>(0u), kLayerBytes158, sizeof(kLayerBytes158)},
    {4237051125u, 262198775u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {4237051125u, 1588818980u, static_cast<std::uint8_t>(0u), kLayerBytes160, sizeof(kLayerBytes160)},
    {4237051125u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {4265548251u, 2178182515u, static_cast<std::uint8_t>(0u), &kEmptyLayerByte, 0},
    {4265548251u, 3486612257u, static_cast<std::uint8_t>(0u), kLayerBytes163, sizeof(kLayerBytes163)},
    {4265548251u, 3951442439u, static_cast<std::uint8_t>(0u), kLayerBytes164, sizeof(kLayerBytes164)},
};

inline constexpr apptraverse::EmbeddedDistilledSupply kSupplyConstants = {kSupplyConstantIds, 2};

inline constexpr apptraverse::EmbeddedDistilledGraph kEmbeddedGraph = {1u, kSupplyConstants, kLayers, 165};

}  // namespace window_catalog
}  // namespace embedded_distilled
}  // namespace apptraverse

#endif  // APPTRAVERSE_EMBEDDED_DISTILLED_WINDOW_CATALOG_SUPPLY_H_
