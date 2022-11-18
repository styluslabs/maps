#add_subdirectory(app/deps)

add_library(maps-app
  app/src/bookmarks.cpp
  app/src/mapsapp.cpp
  app/src/mapsearch.cpp
  app/src/mapsources.cpp
  app/src/offlinemaps.cpp
  app/src/resources.cpp
  app/src/tracks.cpp
  app/src/util.cpp
)

#imgui_impl_generic.cpp

target_include_directories(maps-app
  PUBLIC
  core/include/tangram
  app/include
  PRIVATE
  app/deps
  platforms/common/imgui
  core/src
  core/deps
  core/deps/glm
  core/deps/yaml-cpp/include
  core/deps/SQLiteCpp
  core/deps/isect2d/include
)

#core/deps/isect2d/include
#core/deps/variant/include/mapbox
#core/deps/alfons/src
#core/deps/harfbuzz-icu-freetype/harfbuzz/src
#core/deps/harfbuzz-icu-freetype/icu/common
#core/deps/rapidjson

target_compile_definitions(maps-app PRIVATE GLM_FORCE_CTOR_INIT)
