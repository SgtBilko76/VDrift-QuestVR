cmake_minimum_required(VERSION 3.22)
project(vdriftvr C CXX)

# VDrift on Meta Quest (standalone, OpenXR). One shared library, libvdriftvr.so:
#   - the Team Beef OpenXR framework (src/vr/tbxr, from QuakeQuest via SpeedDreamsVR)
#   - the VR glue (src/vr): app thread, stereo camera, controller input, paths, config
#   - the whole VDrift game (../vdrift, branch quest-port; Android changes under __ANDROID__)
#   - the third-party dependencies, built from source: bullet3, ogg, vorbis, zlib, libpng
# VDrift's own shader-based GL2 renderer runs on GLES 3.2 directly - no gl4es.

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(BUILD_SHARED_LIBS OFF)

# OpenXR loader + headers via the Maven AAR (prefab).
find_package(OpenXR REQUIRED CONFIG)

# VDrift source tree (git repo, branch quest-port).
set(VD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../vdrift" CACHE PATH "VDrift source tree")
get_filename_component(VD_ROOT "${VD_ROOT}" ABSOLUTE)
if(NOT EXISTS "${VD_ROOT}/src/game.h")
    message(FATAL_ERROR "VD_ROOT=${VD_ROOT} does not look like the VDrift source tree")
endif()
set(VD_SRC ${VD_ROOT}/src)

set(VR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/vr)
set(TP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
set(GEN_INC ${CMAKE_BINARY_DIR}/gen_include)
file(MAKE_DIRECTORY ${GEN_INC})

include(cmake/deps.cmake)            # zlib, libpng, ogg, vorbis, bullet3, enet
include(cmake/vdrift_sources.cmake)  # VDRIFT_GAME_SOURCES

# ---------------------------------------------------------------------------
# definitions.h: SCons writes this for the desktop build.
# ---------------------------------------------------------------------------
execute_process(COMMAND git -C ${VD_ROOT} rev-parse --short HEAD
                OUTPUT_VARIABLE VD_REV OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT VD_REV)
    set(VD_REV "unknown")
endif()
file(WRITE ${GEN_INC}/definitions.h
"#ifndef _DEFINITIONS_H
#define _DEFINITIONS_H
#define VERSION \"quest-port\"
#define REVISION \"${VD_REV}\"
#define OS_NAME \"Android\"
#define SETTINGS_DIR \".vdrift\"
#define DATA_DIR \"/sdcard/VDriftVR/data\"
#define LOCALE_DIR \"/sdcard/VDriftVR/data/locale\"
#endif // _DEFINITIONS_H
")

# ---------------------------------------------------------------------------
# The app
# ---------------------------------------------------------------------------
add_library(vdriftvr SHARED
    ${VR_DIR}/tbxr/TBXR_Common.c
    ${VR_DIR}/tbxr/OpenXrInput.c
    ${VR_DIR}/vdrift_vr.cpp
    ${VR_DIR}/vr_paths.cpp
    ${VR_DIR}/vr_config.cpp
    ${VR_DIR}/vr_events.cpp
    ${VR_DIR}/vr_input.cpp
    ${VR_DIR}/vr_stereo.cpp
    ${VR_DIR}/vr_perf.cpp
    # Android replacements for VDrift's SDL-based platform files
    ${VD_SRC}/android/glcore_gles.cpp
    ${VD_SRC}/android/window_android.cpp
    ${VD_SRC}/android/eventsystem_android.cpp
    ${VD_SRC}/android/forcefeedback_android.cpp
    ${VD_SRC}/android/http_stub.cpp
    ${VD_SRC}/net/netclient.cpp
    ${VDRIFT_GAME_SOURCES}
)
set_source_files_properties(${VD_SRC}/main.cpp PROPERTIES COMPILE_DEFINITIONS "main=VdriftMain")

target_include_directories(vdriftvr PRIVATE
    ${VR_DIR}
    ${VR_DIR}/tbxr
    ${VD_SRC}
    ${VD_SRC}/android          # SDL3/ keycode compat headers
    ${GEN_INC}
)
target_compile_definitions(vdriftvr PRIVATE ANDROID VDRIFT_VR)
target_compile_options(vdriftvr PRIVATE
    -Wall -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
    -Wno-unused-but-set-variable -Wno-deprecated-declarations -Wno-sign-compare
    -Wno-reorder-ctor -Wno-missing-braces -Wno-unused-private-field
    -fno-strict-aliasing)

target_link_libraries(vdriftvr PRIVATE
    vd_bullet
    vorbisfile vorbis ogg
    png zlib
    enet
    OpenXR::openxr_loader
    GLESv3
    EGL
    aaudio
    android
    log
)
target_link_options(vdriftvr PRIVATE -Wl,-z,max-page-size=16384)
