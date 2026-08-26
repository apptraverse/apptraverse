/*
 * Object-system build config. Telemetry compiled out. No Aether client,
 * registration, or cloud DNS.
 */
#ifndef APPTRAVERSE_AETHER_USER_CONFIG_H_
#define APPTRAVERSE_AETHER_USER_CONFIG_H_

#include "aether/config_consts.h"

#define AE_TELE_ENABLED 0
#define AE_TELE_LOG_CONSOLE 0
#define AE_TELE_LOG_TO_STATISTICS 0
#define AE_SUPPORT_REGISTRATION 0
#define AE_SUPPORT_CLOUD_DNS 0

#endif /* APPTRAVERSE_AETHER_USER_CONFIG_H_ */
