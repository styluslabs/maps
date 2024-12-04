cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 14)

project(ascend)

if(EXISTS "${PROJECT_SOURCE_DIR}/styluslabs")
  set(STYLUSLABS_DEPS "styluslabs")
else()
  set(STYLUSLABS_DEPS "deps")
endif()

add_definitions(-DMAPS_GIT_REV=${MAPS_GIT_REV})

if(CMAKE_BUILD_TYPE MATCHES Debug)
  add_definitions(-DDEBUG)
  add_definitions(-DLOG_LEVEL=3)
else()
  add_definitions(-DNDEBUG)
  add_definitions(-DLOG_LEVEL=2)
  #add_definitions(-DGLM_FORCE_INTRINSICS)
  # print glm config to ensure SIMD is being used
  #add_definitions(-DGLM_FORCE_MESSAGES)
endif()

option(MAPS_USE_ASAN "Enable Address Sanitizer." OFF)
if (MAPS_USE_ASAN)
  message (STATUS "Enabling Address Sanitizer")
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
  add_definitions(-DMAPS_USE_ASAN)
endif (MAPS_USE_ASAN)

# TSAN and ASAN cannot be used together
option(MAPS_USE_TSAN "Enable Thread Sanitizer." OFF)
if (MAPS_USE_TSAN)
  message (STATUS "Enabling Thread Sanitizer")
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
  add_definitions(-DMAPS_USE_TSAN)
endif (MAPS_USE_TSAN)

# executable
include("app/${TANGRAM_PLATFORM}/config.cmake")
# app library
include("app/config.cmake")
# tangram library
add_subdirectory(tangram-es)
target_include_directories(tangram-core PRIVATE "${STYLUSLABS_DEPS}/nanovgXC/src")

if(TANGRAM_BUILD_BENCHMARKS OR TANGRAM_BUILD_TESTS)
  target_include_directories(platform_mock
    PRIVATE
    "${STYLUSLABS_DEPS}/nanovgXC/src"
    tangram-es/core/deps/stb
  )
endif()

if(CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
  target_compile_definitions(tangram-core PRIVATE TANGRAM_JS_TRACING=1)
  target_compile_definitions(tangram-core PRIVATE TANGRAM_RELWITHDEBINFO=1)
endif()
