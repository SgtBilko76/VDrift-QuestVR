cmake_minimum_required(VERSION 3.16)
project(vdrift-server CXX C)

# The VDrift dedicated multiplayer server (Linux). Builds the physics, track,
# AI and content-loading parts of VDrift from ../../vdrift (branch quest-port)
# into a static library, with a null GL layer in place of a renderer, and links
# the server on top: no window, no sound, no GUI.
#
#   cmake -S server -B build-server -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-server -j
#
# Needs: g++, cmake, libbullet-dev, libenet-dev, libpng-dev, zlib1g-dev, libvorbis-dev.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(VD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../vdrift" CACHE PATH "VDrift source tree")
get_filename_component(VD_ROOT "${VD_ROOT}" ABSOLUTE)
if(NOT EXISTS "${VD_ROOT}/src/track.h")
    message(FATAL_ERROR "VD_ROOT=${VD_ROOT} does not look like the VDrift source tree")
endif()
set(VD_SRC ${VD_ROOT}/src)

find_package(PkgConfig REQUIRED)
pkg_check_modules(BULLET REQUIRED bullet)
pkg_check_modules(PNG REQUIRED libpng)
pkg_check_modules(VORBISFILE REQUIRED vorbisfile)
find_library(ENET_LIB enet REQUIRED)
find_path(ENET_INCLUDE enet/enet.h REQUIRED)

set(GEN_INC ${CMAKE_BINARY_DIR}/gen_include)
file(MAKE_DIRECTORY ${GEN_INC})
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
#define OS_NAME \"Linux\"
#define SETTINGS_DIR \".vdrift\"
#define DATA_DIR \"data\"
#define LOCALE_DIR \"data/locale\"
#endif // _DEFINITIONS_H
")

# Everything the game has except the platform layer (SDL window, events, audio
# device, haptics, curl) and the GL loader. The archive only contributes the
# objects the server references, so unused parts (renderer, GUI) cost nothing.
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/vdrift_sources.cmake)
set(VD_SERVER_SOURCES ${VDRIFT_GAME_SOURCES})
list(FILTER VD_SERVER_SOURCES EXCLUDE REGEX "/(main|game|carcontrolmap|svn_sourceforge|updatemanager|autoupdate|downloadable)\\.cpp$")
list(FILTER VD_SERVER_SOURCES EXCLUDE REGEX "/gui/")
list(FILTER VD_SERVER_SOURCES EXCLUDE REGEX "/sound/sound\\.cpp$")
list(FILTER VD_SERVER_SOURCES EXCLUDE REGEX "/graphics/(graphics_gl2|render_input_scene|render_input_postprocess|render_output|shader|sky|fbobject|fbtexture)\\.cpp$")

add_library(vdcore STATIC ${VD_SERVER_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/glcore_null.cpp ${CMAKE_CURRENT_SOURCE_DIR}/glwrapper_stub.cpp)
target_include_directories(vdcore PUBLIC ${VD_SRC} ${GEN_INC} ${BULLET_INCLUDE_DIRS} ${PNG_INCLUDE_DIRS})
target_compile_definitions(vdcore PUBLIC VDRIFT_SERVER)
target_compile_options(vdcore PRIVATE -Wall -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
    -Wno-unused-but-set-variable -Wno-deprecated-declarations -Wno-sign-compare -Wno-reorder
    -Wno-misleading-indentation -Wno-class-memaccess -fno-strict-aliasing)
target_link_libraries(vdcore PUBLIC ${BULLET_LIBRARIES} ${PNG_LIBRARIES} ${VORBISFILE_LIBRARIES} z pthread)
target_link_directories(vdcore PUBLIC ${BULLET_LIBRARY_DIRS})

add_executable(vdrift-server
    ${VD_SRC}/net/netserver.cpp
    ${VD_SRC}/net/server_main.cpp)
target_include_directories(vdrift-server PRIVATE ${ENET_INCLUDE})
target_link_libraries(vdrift-server PRIVATE vdcore ${ENET_LIB})
target_compile_options(vdrift-server PRIVATE -Wall -Wno-unused-parameter)

install(TARGETS vdrift-server RUNTIME DESTINATION bin)
