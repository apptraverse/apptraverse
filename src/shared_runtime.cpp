#include "apptraverse/shared_runtime.h"

namespace apptraverse {

SharedRuntime::SharedRuntime(SharedRuntimeConfig config)
    : config_{std::move(config)} {}

}  // namespace apptraverse
