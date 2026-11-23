/*
 * Aether build config for AppTraverse.
 * Telemetry compiled out. Registration + cloud DNS enabled for chat runtime.
 * AE_FILTRATION is set on the aether target so AetherApp loads persistent
 * client state instead of wiping storage on Construct.
 */
#ifndef APPTRAVERSE_AETHER_USER_CONFIG_H_
#define APPTRAVERSE_AETHER_USER_CONFIG_H_

#define AE_TELE_ENABLED 0
#define AE_TELE_LOG_CONSOLE 0
#define AE_TELE_LOG_TO_STATISTICS 0
#define AE_SUPPORT_REGISTRATION 1
#define AE_SUPPORT_CLOUD_DNS 1

// Object storage codecs. Build exporters and consumers with the same embedded
// codec. Convert saved snapshots before changing the persistence codec.
// Available values: AE_OBJECTS_CODEC_LZMA, AE_OBJECTS_CODEC_ZLIB,
// AE_OBJECTS_CODEC_LZ4, AE_OBJECTS_CODEC_HEATSHRINK.
#define AE_OBJECTS_EMBEDDED_COMPRESSION AE_OBJECTS_CODEC_LZMA
#define AE_OBJECTS_PERSISTENCE_COMPRESSION AE_OBJECTS_CODEC_LZ4

#endif /* APPTRAVERSE_AETHER_USER_CONFIG_H_ */
