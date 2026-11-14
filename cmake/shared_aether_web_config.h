#ifndef APPTRAVERSE_SHARED_AETHER_WEB_CONFIG_H_
#define APPTRAVERSE_SHARED_AETHER_WEB_CONFIG_H_
#include "shared_aether_config.h"
// Native UDP is already disabled in the shared configuration. Browser WebSocket
// APIs own DNS and TLS; no c-ares/native socket proxy is needed.
#undef AE_SUPPORT_TCP
#define AE_SUPPORT_TCP 0
#undef AE_SUPPORT_WEBSOCKET
#define AE_SUPPORT_WEBSOCKET 1
#undef AE_SUPPORT_CLOUD_DNS
#define AE_SUPPORT_CLOUD_DNS 0
#endif
