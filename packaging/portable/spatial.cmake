# Client-only, checksum-pinned spatial dependencies. Never fetch while configuring.
set(iron_spatial_deps "${IRON_DEPS_DIR}")
if(NOT iron_spatial_deps)
  set(iron_spatial_deps "${CMAKE_CURRENT_SOURCE_DIR}/.deps/src")
endif()
foreach(dep jplspatial libmysofa zlib)
  if(NOT EXISTS "${iron_spatial_deps}/${dep}/CMakeLists.txt")
    message(FATAL_ERROR "Missing ${dep}: run python3 packaging/portable/fetch-deps.py --platform linux (or your target platform)")
  endif()
endforeach()
set(JPL_ENABLE_AVX2_FMA OFF CACHE BOOL "" FORCE)
set(JPL_ENABLE_IPO OFF CACHE BOOL "" FORCE)
add_subdirectory("${iron_spatial_deps}/jplspatial" "${CMAKE_BINARY_DIR}/deps/jplspatial" EXCLUDE_FROM_ALL)
# GCC 16 rejects always_inline virtual allocator overrides in this revision.
target_compile_definitions(JPLSpatial PUBLIC JPL_NO_FORCE_INLINE)

if(WIN32)
  # A private static zlib avoids installing a new runtime DLL.
  set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  add_subdirectory("${iron_spatial_deps}/zlib" "${CMAKE_BINARY_DIR}/deps/zlib" EXCLUDE_FROM_ALL)
  set(iron_spatial_zlib zlibstatic)
else()
  find_package(ZLIB REQUIRED)
  set(iron_spatial_zlib ZLIB::ZLIB)
endif()

# Build the documented C API directly as a static dependency. This avoids
# upstream's mixed static/shared export-header ambiguity on MinGW and ensures PIC
# when the library is linked into the Android client shared object.
set(mysofa_root "${iron_spatial_deps}/libmysofa/src")
set(mysofa_generated "${CMAKE_CURRENT_BINARY_DIR}/spatial-generated")
file(MAKE_DIRECTORY "${mysofa_generated}")
file(WRITE "${mysofa_generated}/mysofa_export.h" "#pragma once\n#define MYSOFA_EXPORT\n")
file(WRITE "${mysofa_generated}/config.h" "#pragma once\n#define CMAKE_INSTALL_PREFIX \"\"\n#define CPACK_PACKAGE_VERSION_MAJOR 1\n#define CPACK_PACKAGE_VERSION_MINOR 3\n#define CPACK_PACKAGE_VERSION_PATCH 5\n")
set(mysofa_sources
  hrtf/reader.c hdf/superblock.c hdf/dataobject.c hdf/btree.c hdf/fractalhead.c
  hdf/gunzip.c hdf/gcol.c hrtf/check.c hrtf/spherical.c hrtf/lookup.c hrtf/tools.c
  hrtf/kdtree.c hrtf/neighbors.c hrtf/interpolate.c hrtf/resample.c hrtf/loudness.c
  hrtf/minphase.c hrtf/easy.c hrtf/cache.c resampler/speex_resampler.c)
list(TRANSFORM mysofa_sources PREPEND "${mysofa_root}/")
add_library(iron-mysofa STATIC ${mysofa_sources})
set_target_properties(iron-mysofa PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 99)
target_include_directories(iron-mysofa PUBLIC "${mysofa_root}/hrtf" "${mysofa_generated}")
target_link_libraries(iron-mysofa PUBLIC ${iron_spatial_zlib})
if(NOT WIN32)
  target_link_libraries(iron-mysofa PRIVATE m)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(hrtf_binary "${CMAKE_CURRENT_SOURCE_DIR}/data/hrtf/kemar-48k.bin.z")
set(hrtf_manifest "${CMAKE_CURRENT_SOURCE_DIR}/data/hrtf/manifest.json")
set(hrtf_cpp "${mysofa_generated}/hrtf_data.cpp")
add_custom_command(OUTPUT "${hrtf_cpp}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_hrtf.py" "${hrtf_binary}" "${hrtf_manifest}" "${hrtf_cpp}"
  DEPENDS "${hrtf_binary}" "${hrtf_manifest}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_hrtf.py"
  VERBATIM)
add_library(iron-spatial STATIC src/spatial_audio.cpp "${hrtf_cpp}")
set_target_properties(iron-spatial PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(iron-spatial PUBLIC include)
target_include_directories(iron-spatial SYSTEM PRIVATE "${iron_spatial_deps}/jplspatial/Spatialization/include")
target_link_libraries(iron-spatial PRIVATE JPL::Spatial iron-mysofa)
target_compile_options(iron-spatial PRIVATE -Wall -Wextra -Wpedantic)
