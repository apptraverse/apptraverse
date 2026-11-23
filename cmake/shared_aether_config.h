#ifndef APPTRAVERSE_SHARED_AETHER_CONFIG_H_
#define APPTRAVERSE_SHARED_AETHER_CONFIG_H_
#include "aether/config_consts.h"

// Windows chat enables native peer UDP and WSS alongside Æther cloud TCP.
// Other platforms retain their prior transport and crypto profile.
// Identity loading is selected by AE_FILTRATION in CMake.
#if defined(_WIN32)
#  define AE_CRYPTO_ASYNC AE_SODIUM_BOX_SEAL
#  define AE_CRYPTO_SYNC AE_CHACHA20_POLY1305
#  define AE_SIGNATURE AE_ED25519
#  define AE_KDF AE_SODIUM_KDF
#else
#  define AE_CRYPTO_ASYNC AE_HYDRO_CRYPTO_PK
#  define AE_CRYPTO_SYNC AE_HYDRO_CRYPTO_SK
#  define AE_SIGNATURE AE_HYDRO_SIGNATURE
#  define AE_KDF AE_HYDRO_KDF
#endif
#define AE_SUPPORT_TCP 1
#if defined(_WIN32)
#  define AE_SUPPORT_IPV6 1
#  define AE_SUPPORT_UDP 1
#  define AE_SUPPORT_LAN 1
#  define AE_SUPPORT_HOLE_PUNCH 1
#  define AE_SUPPORT_WEBSOCKET 1
#  define AE_SUPPORT_HTTP 1
#  define AE_SUPPORT_HTTPS 1
#else
#  define AE_SUPPORT_IPV6 0
#  define AE_SUPPORT_UDP 0
#  define AE_SUPPORT_LAN 0
#  define AE_SUPPORT_HOLE_PUNCH 0
#  define AE_SUPPORT_WEBSOCKET 0
#  define AE_SUPPORT_HTTP 0
#  define AE_SUPPORT_HTTPS 0
#endif
#define AE_SUPPORT_PROXY 0
#define AE_SUPPORT_WIFIS 0
#define AE_TELE_ENABLED 1
#define AE_TELE_LOG_CONSOLE 0
#define AE_TELE_LOG_TO_STATISTICS 1
#define AE_SUPPORT_REGISTRATION 1
#ifndef AE_DNS_BACKEND
#define AE_DNS_BACKEND AE_DNS_SYSTEM
#endif
#define AE_EVENTS_MAX_COUNT 4096
#define AE_TASK_MAX_COUNT 1024
#define AE_TCP_PACKET_QUEUE_SIZE 512
#define AE_PING_INTERVAL_MS 1000
#endif
