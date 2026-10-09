cmake_minimum_required(VERSION 3.5.0)
project(isaac_ohos)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Generated guest translation units (190 files, ~208 MB of C)
file(GLOB ISAAC_GENERATED_SRCS
  "${CMAKE_CURRENT_SOURCE_DIR}/isaac/generated/*.c"
)

add_library(entry SHARED
  main_ohos.cpp
  backend_gles.cpp
  backend_vk.cpp
  # Guest runtime
  isaac/guest_memory.c
  isaac/guest.c
  isaac/isaac_guest_main.c
  isaac/stubs.c
  # Platform shim layer and extension modules (see isaac/host_ext.h).
  # Listed explicitly: a missing entry here is a link error, not a silent
  # fallback, and CMake globs do not re-run on their own.
  isaac/host_ohos.c
  isaac/host_ext_printf.c
  isaac/host_ext_misc.c
  isaac/host_ext_file.c
  isaac/host_ext_path.c
  isaac/host_kage.c
  # Hand-translated missing functions (build_one.py extras)
  isaac/generated/guest_extra_002597b0.c
  isaac/generated/guest_extra_00259650.c
  isaac/generated/guest_extra_002599c0.c
  isaac/generated/guest_extra_002c8700.c
  isaac/generated/guest_extra_00481280.c
  # Graphics boundary
  isaac/glsl_es_rewrite.c
  isaac/gl_ohos_backend.c
  ${ISAAC_GENERATED_SRCS}
)

target_include_directories(entry PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/isaac
  ${CMAKE_CURRENT_SOURCE_DIR}/isaac/generated
)

target_link_libraries(entry PUBLIC
  libace_napi.z.so
  libace_ndk.z.so
  libEGL.so
  libGLESv3.so
  libnative_window.so
  libhilog_ndk.z.so
  libvulkan.so
  libdl.so
)
