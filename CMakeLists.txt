cmake_minimum_required(VERSION 3.10)

project(explore)

if(EXISTS "${PROJECT_SOURCE_DIR}/styluslabs")
  set(STYLUSLABS_DEPS "styluslabs")
else()
  set(STYLUSLABS_DEPS "deps")
endif()

# tangram library
add_subdirectory(tangram-es)
# app library
include("app/config.cmake")
# executable
include("app/${TANGRAM_PLATFORM}/config.cmake")
