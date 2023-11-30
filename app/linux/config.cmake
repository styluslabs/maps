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

check_unsupported_compiler_version()

add_definitions(-DTANGRAM_LINUX)

set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)

include(cmake/glfw.cmake)

# System font config
include(FindPkgConfig)
pkg_check_modules(FONTCONFIG REQUIRED "fontconfig")

find_package(CURL REQUIRED)

add_executable(tangram
  platforms/linux/src/linuxPlatform.cpp
  platforms/common/platform_gl.cpp
  platforms/common/urlClient.cpp
  platforms/common/linuxSystemFontHelper.cpp
)

target_include_directories(tangram
  PRIVATE
  platforms/common
  app/include
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

# to be consistent w/ core
target_compile_definitions(tangram PRIVATE GLM_FORCE_CTOR_INIT)

#add_resources(tangram "${PROJECT_SOURCE_DIR}/scenes" "res")
