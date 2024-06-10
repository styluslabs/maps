cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 14)

project(explore)

if(EXISTS "${PROJECT_SOURCE_DIR}/styluslabs")
  set(STYLUSLABS_DEPS "styluslabs")
else()
  set(STYLUSLABS_DEPS "deps")
endif()

if(CMAKE_BUILD_TYPE MATCHES Debug)
  add_definitions(-DDEBUG)
  add_definitions(-DLOG_LEVEL=3)
else()
  add_definitions(-DNDEBUG)
  add_definitions(-DLOG_LEVEL=2)
endif()

# executable
include("app/${TANGRAM_PLATFORM}/config.cmake")
# app library
include("app/config.cmake")
# tangram library
add_subdirectory(tangram-es)
target_include_directories(tangram-core PRIVATE "${STYLUSLABS_DEPS}/nanovgXC/src")
