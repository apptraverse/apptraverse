/*
 * App Traverse-owned Aether user configuration for the object-system build.
 * Telemetry is compiled out. Registration/cloud DNS stay available because
 * Æther's Domain/Obj stack is still built with distillation enabled.
 */
#ifndef APPTRAVERSE_AETHER_USER_CONFIG_H_
#define APPTRAVERSE_AETHER_USER_CONFIG_H_

#include "aether/config_consts.h"

#define AE_CRYPTO_ASYNC AE_HYDRO_CRYPTO_PK
#define AE_CRYPTO_SYNC AE_HYDRO_CRYPTO_SK
#define AE_SIGNATURE AE_HYDRO_SIGNATURE
#define AE_KDF AE_HYDRO_KDF

#if ESP_PLATFORM
#  define AE_CLOUD_MAX_SERVER_CONNECTIONS 1
#  define AE_SAFE_STREAM_CAPACITY 2 * 1024
#endif

#if !ESP_PLATFORM
#  define AE_SUPPORT_WIFIS 0
#endif

#define AE_TELE_ENABLED 0
#define AE_TELE_LOG_CONSOLE 0
#define AE_TELE_LOG_TO_STATISTICS 0

#if AE_DISTILLATION || AE_FILTRATION
#  define AE_SUPPORT_REGISTRATION 1
#  define AE_SUPPORT_CLOUD_DNS 1
#else
#  define AE_SUPPORT_REGISTRATION 0
#  define AE_SUPPORT_CLOUD_DNS 0
#endif

#endif /* APPTRAVERSE_AETHER_USER_CONFIG_H_ */
