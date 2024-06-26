add_definitions(-DTANGRAM_ANDROID)

add_library(droidmaps SHARED
  app/android/app/src/main/cpp/androidApp.cpp
  tangram-es/platforms/common/platform_gl.cpp
  tangram-es/platforms/android/tangram/src/main/cpp/JniHelpers.cpp
  tangram-es/platforms/android/tangram/src/main/cpp/AndroidPlatform.cpp
  tangram-es/platforms/android/tangram/src/main/cpp/sqlite_fdvfs.c
)

target_include_directories(droidmaps PRIVATE
  tangram-es/platforms/android/tangram/src/main/cpp
  tangram-es/platforms/common
  tangram-es/core/deps
  tangram-es/core/deps/glm
  tangram-es/core/deps/yaml-cpp/include
  tangram-es/core/deps/stb
  tangram-es/core/deps/sqlite3
  ${STYLUSLABS_DEPS}
  ${STYLUSLABS_DEPS}/nanovgXC/src
  ${STYLUSLABS_DEPS}/pugixml/src
  ${STYLUSLABS_DEPS}/SDL/include
)

#target_compile_definitions(droidmaps PRIVATE GLM_FORCE_CTOR_INIT)

if(TANGRAM_MBTILES_DATASOURCE)
  target_sources(droidmaps PRIVATE tangram-es/platforms/android/tangram/src/main/cpp/sqlite3ndk.cpp)
  target_include_directories(droidmaps PRIVATE tangram-es/core/deps/sqlite3) # sqlite3ndk.cpp needs sqlite3.h
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
