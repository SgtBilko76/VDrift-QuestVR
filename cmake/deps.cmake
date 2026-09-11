# Third-party dependencies, all built from source (static) for arm64-v8a.
#   zlib, libpng   : third_party/zlib, third_party/libpng (vendored copies from SpeedDreamsVR)
#   ogg, vorbis    : git clones (v1.3.5, v1.3.7)
#   bullet3        : git clone (3.25)

set(DEP_C_QUIET -Wno-deprecated-non-prototype -Wno-implicit-function-declaration -Wno-int-conversion
    -Wno-deprecated-declarations -Wno-unused-but-set-variable -Wno-unused-function -Wno-unused-variable)

# ---------------------------------------------------------------- zlib
set(ZLIB_DIR ${TP_DIR}/zlib)
add_library(zlib STATIC
    ${ZLIB_DIR}/adler32.c ${ZLIB_DIR}/compress.c ${ZLIB_DIR}/crc32.c ${ZLIB_DIR}/deflate.c
    ${ZLIB_DIR}/gzclose.c ${ZLIB_DIR}/gzlib.c ${ZLIB_DIR}/gzread.c ${ZLIB_DIR}/gzwrite.c
    ${ZLIB_DIR}/infback.c ${ZLIB_DIR}/inffast.c ${ZLIB_DIR}/inflate.c ${ZLIB_DIR}/inftrees.c
    ${ZLIB_DIR}/trees.c ${ZLIB_DIR}/uncompr.c ${ZLIB_DIR}/zutil.c)
target_include_directories(zlib PUBLIC ${ZLIB_DIR})
target_compile_definitions(zlib PRIVATE HAVE_UNISTD_H Z_HAVE_UNISTD_H)
target_compile_options(zlib PRIVATE ${DEP_C_QUIET})

# ---------------------------------------------------------------- libpng
set(PNG_DIR ${TP_DIR}/libpng)
set(PNG_GEN ${CMAKE_BINARY_DIR}/gen_png)
file(MAKE_DIRECTORY ${PNG_GEN})
configure_file(${PNG_DIR}/scripts/pnglibconf.h.prebuilt ${PNG_GEN}/pnglibconf.h COPYONLY)
add_library(png STATIC
    ${PNG_DIR}/png.c ${PNG_DIR}/pngerror.c ${PNG_DIR}/pngget.c ${PNG_DIR}/pngmem.c
    ${PNG_DIR}/pngpread.c ${PNG_DIR}/pngread.c ${PNG_DIR}/pngrio.c ${PNG_DIR}/pngrtran.c
    ${PNG_DIR}/pngrutil.c ${PNG_DIR}/pngset.c ${PNG_DIR}/pngtrans.c ${PNG_DIR}/pngwio.c
    ${PNG_DIR}/pngwrite.c ${PNG_DIR}/pngwtran.c ${PNG_DIR}/pngwutil.c)
target_include_directories(png PUBLIC ${PNG_DIR} ${PNG_GEN})
target_compile_definitions(png PRIVATE PNG_ARM_NEON_OPT=0)
target_link_libraries(png PUBLIC zlib)
target_compile_options(png PRIVATE ${DEP_C_QUIET})

# ---------------------------------------------------------------- ogg
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(INSTALL_CMAKE_PACKAGE_MODULE OFF CACHE BOOL "" FORCE)
add_subdirectory(${TP_DIR}/ogg EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------- vorbis (+vorbisfile), from the lib sources directly
# vorbis's own CMakeLists insists on find_package(Ogg); the library is a flat list of C files.
set(VORBIS_DIR ${TP_DIR}/vorbis)
file(GLOB VORBIS_LIB_SRCS ${VORBIS_DIR}/lib/*.c)
list(FILTER VORBIS_LIB_SRCS EXCLUDE REGEX "psytune|tone\\.c|barkmel|vorbisfile|vorbisenc")
add_library(vorbis STATIC ${VORBIS_LIB_SRCS})
target_include_directories(vorbis PUBLIC ${VORBIS_DIR}/include PRIVATE ${VORBIS_DIR}/lib)
target_link_libraries(vorbis PUBLIC ogg)
target_compile_options(vorbis PRIVATE ${DEP_C_QUIET})
add_library(vorbisfile STATIC ${VORBIS_DIR}/lib/vorbisfile.c)
target_include_directories(vorbisfile PUBLIC ${VORBIS_DIR}/include PRIVATE ${VORBIS_DIR}/lib)
target_link_libraries(vorbisfile PUBLIC vorbis)
target_compile_options(vorbisfile PRIVATE ${DEP_C_QUIET})

# ---------------------------------------------------------------- bullet3
set(BUILD_BULLET3 OFF CACHE BOOL "" FORCE)
set(BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(BUILD_PYBULLET OFF CACHE BOOL "" FORCE)
set(BUILD_BULLET2_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_OPENGL3_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_CPU_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_ENET OFF CACHE BOOL "" FORCE)
set(BUILD_CLSOCKET OFF CACHE BOOL "" FORCE)
set(INSTALL_LIBS OFF CACHE BOOL "" FORCE)
set(INSTALL_CMAKE_FILES OFF CACHE BOOL "" FORCE)
set(USE_GRAPHICAL_BENCHMARK OFF CACHE BOOL "" FORCE)
set(USE_DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
set(USE_GLUT OFF CACHE BOOL "" FORCE)
add_subdirectory(${TP_DIR}/bullet3 EXCLUDE_FROM_ALL)
add_library(vd_bullet INTERFACE)
target_include_directories(vd_bullet INTERFACE ${TP_DIR}/bullet3/src)
target_link_libraries(vd_bullet INTERFACE BulletDynamics BulletCollision LinearMath)
foreach(t BulletDynamics BulletCollision LinearMath BulletSoftBody)
    if(TARGET ${t})
        target_compile_options(${t} PRIVATE -Wno-everything)
    endif()
endforeach()
