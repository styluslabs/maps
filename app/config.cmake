add_library(maps-app
  app/src/bookmarks.cpp
  app/src/mapsapp.cpp
  app/src/mapsearch.cpp
  app/src/mapsources.cpp
  app/src/offlinemaps.cpp
  app/src/resources.cpp
  app/src/touchhandler.cpp
  app/src/tracks.cpp
  app/src/trackwidgets.cpp
  app/src/util.cpp
  app/src/plugins.cpp
  app/src/mapwidgets.cpp
  app/src/glfwmain.cpp
  # ugui
  app/styluslabs/ugui/svggui.cpp
  app/styluslabs/ugui/widgets.cpp
  app/styluslabs/ugui/textedit.cpp
  app/styluslabs/ulib/geom.cpp
  app/styluslabs/ulib/image.cpp
  app/styluslabs/ulib/path2d.cpp
  app/styluslabs/ulib/painter.cpp
  app/styluslabs/usvg/svgnode.cpp
  app/styluslabs/usvg/svgstyleparser.cpp
  app/styluslabs/usvg/svgparser.cpp
  app/styluslabs/usvg/svgpainter.cpp
  app/styluslabs/usvg/svgwriter.cpp
  #app/styluslabs/usvg/pdfwriter.cpp
  app/styluslabs/usvg/cssparser.cpp
  app/styluslabs/nanovg-2/src/nanovg.c
  app/styluslabs/pugixml/src/pugixml.cpp
  app/styluslabs/ugui/example/glfwSDL.c
)

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
  core/deps/sqlite3
  core/deps/isect2d/include
  core/deps/stb
  app/styluslabs
  app/styluslabs/nanovg-2/src
  app/styluslabs/pugixml/src
  app/styluslabs/SDL/include
  platforms/common
)

target_compile_definitions(maps-app PRIVATE GLM_FORCE_CTOR_INIT)
target_compile_definitions(maps-app PRIVATE PUGIXML_NO_XPATH)
target_compile_definitions(maps-app PRIVATE PUGIXML_NO_EXCEPTIONS)
#target_compile_definitions(maps-app PRIVATE NO_PAINTER_GL)
#target_compile_definitions(maps-app PRIVATE FONS_SDF)

# native file dialogs library
add_subdirectory(app/deps/nfd)
target_link_libraries(maps-app PRIVATE nfd)
