# Central AppTraverse C++ compile policy (no RTTI, reusable by future targets).
#
# Link AppTraverse-owned C++ targets to apptraverse_compile_policy:
# - PUBLIC from the `apptraverse` library (covers normal consumers automatically)
# - PRIVATE when a target does not link `apptraverse` (header-only / helper tests)
#
# Future surfaces_demo / SharedNode targets that link `apptraverse` inherit
# /GR- or -fno-rtti without adding the flag manually.

add_library(apptraverse_compile_policy INTERFACE)

if(MSVC)
  target_compile_options(apptraverse_compile_policy INTERFACE /GR-)
else()
  target_compile_options(apptraverse_compile_policy INTERFACE -fno-rtti)
endif()
