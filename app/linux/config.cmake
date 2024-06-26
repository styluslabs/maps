if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-gnu-zero-variadic-macro-arguments")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lc++ -lc++abi")
endif()

if (CMAKE_COMPILER_IS_GNUCC)
  message(STATUS "Using gcc ${CMAKE_CXX_COMPILER_VERSION}")
  if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 5.1)
    message(STATUS "USE CXX11_ABI")
    add_definitions("-D_GLIBCXX_USE_CXX11_ABI=1")
  endif()
  if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 7.0)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-strict-aliasing")
  endif()
endif()

# -Wshadow ... too many in Tangram to deal with right now
add_compile_options(-Werror=return-type)
#add_compile_options(-Werror=incompatible-pointer-types) -- only for C

add_definitions(-DTANGRAM_LINUX)

set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)

#include(tangram-es/cmake/glfw.cmake)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Build the GLFW example programs")
set(GLFW_BUILD_TESTS OFF CACHE BOOL "Build the GLFW test programs")
set(GLFW_BUILD_DOCS OFF CACHE BOOL "Build the GLFW documentation")
set(GLFW_INSTALL OFF CACHE BOOL "Generate installation target")
add_subdirectory(tangram-es/platforms/common/glfw)

# System font config
include(FindPkgConfig)
pkg_check_modules(FONTCONFIG REQUIRED "fontconfig")

find_package(CURL REQUIRED)

add_executable(tangram
  tangram-es/platforms/linux/src/linuxPlatform.cpp
  tangram-es/platforms/common/platform_gl.cpp
  tangram-es/platforms/common/urlClient.cpp
  tangram-es/platforms/common/linuxSystemFontHelper.cpp
  app/src/glfwmain.cpp
  ${STYLUSLABS_DEPS}/ugui/example/glfwSDL.c
)

target_include_directories(tangram
  PRIVATE
  tangram-es/platforms/common
  tangram-es/core/deps
  tangram-es/core/deps/stb
  tangram-es/core/deps/yaml-cpp/include
  app/include
  ${STYLUSLABS_DEPS}
  ${STYLUSLABS_DEPS}/nanovgXC/src
  ${STYLUSLABS_DEPS}/pugixml/src
  ${FONTCONFIG_INCLUDE_DIRS}
)

# dependencies should come after modules that depend on them (remember to try start-group / end-group to
#  investigate unexplained "undefined reference" errors)
target_link_libraries(tangram
  PRIVATE
  maps-app
  tangram-core
  glfw
  ${GLFW_LIBRARIES}
  ${OPENGL_LIBRARIES}
  ${FONTCONFIG_LDFLAGS}
  ${CURL_LIBRARIES}
  -pthread
  # only used when not using external lib
  -ldl
)

# set exe name
set_target_properties(tangram PROPERTIES OUTPUT_NAME ascend)

target_compile_options(tangram
  PRIVATE
  -std=c++14
  -fno-omit-frame-pointer
  -Wall
  -Wreturn-type
  -Wsign-compare
  -Wignored-qualifiers
  -Wtype-limits
  -Wmissing-field-initializers
)

if(CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
  target_compile_definitions(tangram-core PRIVATE TANGRAM_JS_TRACING=1)
endif()

#add_resources(tangram "${PROJECT_SOURCE_DIR}/scenes" "res")

# native file dialogs library
set(NFD_PORTAL ON)
add_subdirectory(deps/nfd)
target_link_libraries(tangram PRIVATE nfd)
