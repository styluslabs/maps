add_definitions(-DTANGRAM_ANDROID)

add_library(droidmaps SHARED
  app/android/app/src/main/cpp/androidApp.cpp
  platforms/common/platform_gl.cpp
  platforms/android/tangram/src/main/cpp/JniHelpers.cpp
  platforms/android/tangram/src/main/cpp/AndroidPlatform.cpp
)

target_include_directories(droidmaps PRIVATE
  platforms/android/tangram/src/main/cpp
  platforms/common
  core/deps/glm
  core/deps
  core/deps/yaml-cpp/include
  app/styluslabs
  app/styluslabs/nanovg-2/src
  app/styluslabs/SDL/include
)

target_compile_definitions(droidmaps PRIVATE GLM_FORCE_CTOR_INIT)

if(TANGRAM_MBTILES_DATASOURCE)
  target_sources(droidmaps PRIVATE platforms/android/tangram/src/main/cpp/sqlite3ndk.cpp)
  target_include_directories(droidmaps PRIVATE core/deps/sqlite3) # sqlite3ndk.cpp needs sqlite3.h
  target_compile_definitions(droidmaps PRIVATE TANGRAM_MBTILES_DATASOURCE=1)
endif()

target_link_libraries(droidmaps
  PRIVATE
  tangram-core
  maps-app
  # android libraries
  android
  atomic
  GLESv3
  EGL
  log
  z
  jnigraphics
)
