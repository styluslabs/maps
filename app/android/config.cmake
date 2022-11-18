add_definitions(-DTANGRAM_ANDROID)

add_library(droidmaps SHARED
  app/android/app/src/main/cpp/androidApp.cpp
  platforms/common/platform_gl.cpp
  platforms/android/tangram/src/main/cpp/JniHelpers.cpp
  platforms/android/tangram/src/main/cpp/AndroidPlatform.cpp
)

#platforms/android/tangram/src/main/cpp/JniOnLoad.cpp
#platforms/android/tangram/src/main/cpp/AndroidMap.cpp
#platforms/android/tangram/src/main/cpp/NativeMap.cpp

if(TANGRAM_MBTILES_DATASOURCE)
  target_sources(tangram PRIVATE platforms/android/tangram/src/main/cpp/sqlite3ndk.cpp)
  target_include_directories(tangram PRIVATE core/deps/SQLiteCpp/sqlite3) # sqlite3ndk.cpp needs sqlite3.h
  target_compile_definitions(tangram PRIVATE TANGRAM_MBTILES_DATASOURCE=1)
endif()

target_link_libraries(droidmaps
  PRIVATE
  tangram-core
  maps-app
  # android libraries
  android
  atomic
  GLESv2
  log
  z
  jnigraphics
)
