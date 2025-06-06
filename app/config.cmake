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
  app/src/gpxfile.cpp
  app/src/util.cpp
  app/src/plugins.cpp
  app/src/mapwidgets.cpp
  # ugui
  ${STYLUSLABS_DEPS}/ugui/svggui.cpp
  ${STYLUSLABS_DEPS}/ugui/widgets.cpp
  ${STYLUSLABS_DEPS}/ugui/textedit.cpp
  ${STYLUSLABS_DEPS}/ugui/colorwidgets.cpp
  ${STYLUSLABS_DEPS}/ulib/geom.cpp
  ${STYLUSLABS_DEPS}/ulib/image.cpp
  ${STYLUSLABS_DEPS}/ulib/path2d.cpp
  ${STYLUSLABS_DEPS}/ulib/painter.cpp
  ${STYLUSLABS_DEPS}/usvg/svgnode.cpp
  ${STYLUSLABS_DEPS}/usvg/svgstyleparser.cpp
  ${STYLUSLABS_DEPS}/usvg/svgparser.cpp
  ${STYLUSLABS_DEPS}/usvg/svgpainter.cpp
  ${STYLUSLABS_DEPS}/usvg/svgwriter.cpp
  ${STYLUSLABS_DEPS}/usvg/cssparser.cpp
  ${STYLUSLABS_DEPS}/nanovgXC/src/nanovg.c
  ${STYLUSLABS_DEPS}/pugixml/src/pugixml.cpp
  deps/easyexif/exif.cpp
)

target_include_directories(maps-app
  PUBLIC
  tangram-es/core/include/tangram
  app/include
  PRIVATE
  tangram-es/platforms/common
  tangram-es/platforms/common/glfw/include
  tangram-es/core/src
  tangram-es/core/deps
  tangram-es/core/deps/glm
  tangram-es/core/deps/yaml-cpp/include
  tangram-es/core/deps/sqlite3
  tangram-es/core/deps/isect2d/include
  tangram-es/core/deps/stb
  ${STYLUSLABS_DEPS}
  ${STYLUSLABS_DEPS}/nanovgXC/src
  ${STYLUSLABS_DEPS}/pugixml/src
  deps/easyexif
)

target_compile_definitions(maps-app PUBLIC GLM_FORCE_CTOR_INIT)
target_compile_definitions(maps-app PUBLIC PUGIXML_NO_XPATH)
target_compile_definitions(maps-app PUBLIC PUGIXML_NO_EXCEPTIONS)
target_compile_definitions(maps-app PUBLIC SVGGUI_NO_SDL)

# maybe this will prevent cmake from rebuilding everything when git hash changes
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE MAPS_GIT_REV
    OUTPUT_STRIP_TRAILING_WHITESPACE)

set_source_files_properties(
  app/src/plugins.cpp
  PROPERTIES
    COMPILE_OPTIONS "-DMAPS_GIT_REV=${MAPS_GIT_REV}"
)
