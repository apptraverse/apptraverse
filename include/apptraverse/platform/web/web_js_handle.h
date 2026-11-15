#pragma once
#include <cstdint>
#include <emscripten/em_js.h>
#include <emscripten/val.h>

// Emval handles are JavaScript array indices, not linear-memory pointers.
// Keep the JS ABI explicitly i32, then construct the opaque C++ handle. This
// avoids returning a Number into an i64 Wasm import in 64-bit builds.
#define APPTRAVERSE_EM_JS_HANDLE(name, params, args, ...) \
  EM_JS(unsigned, name##Index, params, __VA_ARGS__); \
  emscripten::EM_VAL name params { \
    return reinterpret_cast<emscripten::EM_VAL>( \
        static_cast<std::uintptr_t>(name##Index args)); \
  }
