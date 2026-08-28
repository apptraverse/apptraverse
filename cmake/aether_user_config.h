/*
 * Aether build config for AppTraverse.
 * Telemetry compiled out. Registration + cloud DNS enabled for chat runtime.
 * AE_FILTRATION is set on the aether target so AetherApp loads persistent
 * client state instead of wiping storage on Construct.
 */
#ifndef APPTRAVERSE_AETHER_USER_CONFIG_H_
#define APPTRAVERSE_AETHER_USER_CONFIG_H_

#include "aether/config_consts.h"

#define AE_TELE_ENABLED 0
#define AE_TELE_LOG_CONSOLE 0
#define AE_TELE_LOG_TO_STATISTICS 0
#define AE_SUPPORT_REGISTRATION 1
#define AE_SUPPORT_CLOUD_DNS 1

#endif /* APPTRAVERSE_AETHER_USER_CONFIG_H_ */
