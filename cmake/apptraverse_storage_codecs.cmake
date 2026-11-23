CPMAddPackage(NAME update_lab_7zip GITHUB_REPOSITORY ip7z/7zip
  GIT_TAG d3296e39e1f1f92c7b41c0c3848e65640428d42d DOWNLOAD_ONLY YES)
set(lzma_sources "${update_lab_7zip_SOURCE_DIR}/C")
add_library(apptraverse_lzma1 STATIC
  "${lzma_sources}/LzmaEnc.c" "${lzma_sources}/LzFind.c"
  "${lzma_sources}/CpuArch.c" "${lzma_sources}/LzmaDec.c")
target_compile_definitions(apptraverse_lzma1 PRIVATE Z7_ST)
target_include_directories(apptraverse_lzma1 PUBLIC "${lzma_sources}")

CPMAddPackage(NAME apptraverse_lz4 GITHUB_REPOSITORY lz4/lz4
  GIT_TAG ebb370ca83af193212df4dcbadcc5d87bc0de2f0 DOWNLOAD_ONLY YES)
CPMAddPackage(NAME apptraverse_zlib GITHUB_REPOSITORY madler/zlib
  GIT_TAG 570720b0c24f9686c33f35a1b3165c1f568b96be DOWNLOAD_ONLY YES)
CPMAddPackage(NAME apptraverse_heatshrink GITHUB_REPOSITORY atomicobject/heatshrink
  GIT_TAG b9ac05e912e1184b55c5d815b280486de10f331f DOWNLOAD_ONLY YES)

set(zlib_sources "${apptraverse_zlib_SOURCE_DIR}")
add_library(apptraverse_storage_codecs STATIC
  "${apptraverse_lz4_SOURCE_DIR}/lib/lz4.c"
  "${apptraverse_heatshrink_SOURCE_DIR}/heatshrink_encoder.c"
  "${apptraverse_heatshrink_SOURCE_DIR}/heatshrink_decoder.c"
  "${zlib_sources}/adler32.c" "${zlib_sources}/crc32.c"
  "${zlib_sources}/deflate.c" "${zlib_sources}/trees.c"
  "${zlib_sources}/inflate.c" "${zlib_sources}/inftrees.c"
  "${zlib_sources}/inffast.c" "${zlib_sources}/zutil.c")
target_compile_definitions(apptraverse_storage_codecs PRIVATE Z_SOLO Z_PREFIX)
target_include_directories(apptraverse_storage_codecs PUBLIC
  "${apptraverse_lz4_SOURCE_DIR}/lib"
  "${apptraverse_heatshrink_SOURCE_DIR}"
  "${zlib_sources}")
target_link_libraries(apptraverse_storage_codecs PUBLIC apptraverse_lzma1)
