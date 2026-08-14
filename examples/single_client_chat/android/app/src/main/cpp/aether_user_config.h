/*
 * AppTraverse-owned Aether user configuration for the Android single-client chat
 * example. Semantics match aether config/user_config_hydrogen.h (registration
 * gated on DISTILLATION/FILTRATION, telemetry on, hydrogen crypto).
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

// telemetry
#define AE_TELE_ENABLED 1
#define AE_TELE_LOG_CONSOLE 1
#define AE_TELE_LOG_TO_STATISTICS 1
#define AE_STATISTICS_MAX_SIZE 1024

// all except MLog
#define AE_TELE_METRICS_MODULES_EXCLUDE {AE_LOG_MODULE}
#define AE_TELE_METRICS_DURATION_EXCLUDE {AE_LOG_MODULE}

#define AE_TELE_LOG_MODULES AE_ALL
#define AE_TELE_DEBUG_MODULES AE_ALL
#define AE_TELE_INFO_MODULES AE_ALL
#define AE_TELE_WARN_MODULES AE_ALL
#define AE_TELE_ERROR_MODULES AE_ALL

#define AE_TELE_LOG_TIME_POINT AE_ALL
// location only for kLog module
#define AE_TELE_LOG_LOCATION {AE_LOG_MODULE}
// tag name for all except kLog
#define AE_TELE_LOG_NAME_EXCLUDE {AE_LOG_MODULE}
#define AE_TELE_LOG_LEVEL_MODULE AE_ALL
#define AE_TELE_LOG_BLOB AE_ALL

#if AE_DISTILLATION || AE_FILTRATION
#  define AE_SUPPORT_REGISTRATION 1
#  define AE_SUPPORT_CLOUD_DNS 1
#else
#  define AE_SUPPORT_REGISTRATION 0
#  define AE_SUPPORT_CLOUD_DNS 0
#endif

#endif /* APPTRAVERSE_AETHER_USER_CONFIG_H_ */
