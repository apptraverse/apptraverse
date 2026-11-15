#pragma once
#include "apptraverse/process_services.h"
namespace apptraverse::windows {
std::unique_ptr<NetworkSubscription> WatchNetwork(NetworkChanged changed);
}
