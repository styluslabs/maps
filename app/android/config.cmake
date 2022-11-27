add_definitions(-DTANGRAM_ANDROID)

add_library(droidmaps SHARED
  app/android/app/src/main/cpp/androidApp.cpp
  app/src/imgui_impl_generic.cpp
  platforms/common/imgui_impl_opengl3.cpp
  platforms/common/platform_gl.cpp
  platforms/android/tangram/src/main/cpp/JniHelpers.cpp
  platforms/android/tangram/src/main/cpp/AndroidPlatform.cpp
)

add_subdirectory(platforms/common/imgui)

target_include_directories(droidmaps PRIVATE
  platforms/android/tangram/src/main/cpp
  platforms/common
  core/deps/glm
)

target_compile_definitions(droidmaps PRIVATE GLM_FORCE_CTOR_INIT)
# for imgui_impl_opengl3
target_compile_definitions(droidmaps PRIVATE IMGUI_IMPL_OPENGL_ES2)

if(TANGRAM_MBTILES_DATASOURCE)
  target_sources(droidmaps PRIVATE platforms/android/tangram/src/main/cpp/sqlite3ndk.cpp)
  target_include_directories(droidmaps PRIVATE core/deps/SQLiteCpp/sqlite3) # sqlite3ndk.cpp needs sqlite3.h
  target_compile_definitions(droidmaps PRIVATE TANGRAM_MBTILES_DATASOURCE=1)
endif()

target_link_libraries(droidmaps
  PRIVATE
  tangram-core
  maps-app
  imgui
  # android libraries
  android
  atomic
  GLESv2
  log
  z
  jnigraphics
)
