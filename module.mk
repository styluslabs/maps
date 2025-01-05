## application ... making a module just so we can have private defs
MODULE_BASE := .

MODULE_SOURCES = \
  app/src/mapsapp.cpp      \
  app/src/bookmarks.cpp    \
  app/src/mapsearch.cpp    \
  app/src/mapsources.cpp   \
  app/src/offlinemaps.cpp  \
  app/src/resources.cpp    \
  app/src/touchhandler.cpp \
  app/src/tracks.cpp       \
  app/src/trackwidgets.cpp \
  app/src/gpxfile.cpp      \
  app/src/util.cpp         \
  app/src/plugins.cpp      \
  app/src/mapwidgets.cpp   \
  $(STYLUSLABS_DEPS)/ugui/svggui.cpp         \
  $(STYLUSLABS_DEPS)/ugui/widgets.cpp        \
  $(STYLUSLABS_DEPS)/ugui/textedit.cpp       \
  $(STYLUSLABS_DEPS)/ugui/colorwidgets.cpp   \
  $(STYLUSLABS_DEPS)/ulib/geom.cpp           \
  $(STYLUSLABS_DEPS)/ulib/image.cpp          \
  $(STYLUSLABS_DEPS)/ulib/path2d.cpp         \
  $(STYLUSLABS_DEPS)/ulib/painter.cpp        \
  $(STYLUSLABS_DEPS)/usvg/svgnode.cpp        \
  $(STYLUSLABS_DEPS)/usvg/svgstyleparser.cpp \
  $(STYLUSLABS_DEPS)/usvg/svgparser.cpp      \
  $(STYLUSLABS_DEPS)/usvg/svgpainter.cpp     \
  $(STYLUSLABS_DEPS)/usvg/svgwriter.cpp      \
  $(STYLUSLABS_DEPS)/usvg/cssparser.cpp      \
  $(STYLUSLABS_DEPS)/nanovgXC/src/nanovg.c   \
  $(STYLUSLABS_DEPS)/pugixml/src/pugixml.cpp \
  deps/easyexif/exif.cpp

#MODULE_INC_PUBLIC = include
MODULE_INC_PRIVATE = $(STYLUSLABS_DEPS) $(STYLUSLABS_DEPS)/nanovgXC/src $(STYLUSLABS_DEPS)/pugixml/src deps/easyexif app/src app/include tangram-es/platforms/common
#MODULE_DEFS_PUBLIC = DBL_CONV_PUBLIC
MODULE_DEFS_PRIVATE = PUGIXML_NO_XPATH PUGIXML_NO_EXCEPTIONS SVGGUI_NO_SDL
#GLM_FORCE_CTOR_INIT

include $(ADD_MODULE)

$(OBJDIR)/$(MODULE_BASE)/app/src/plugins.o: DEFS_PRIVATE += MAPS_GIT_REV=$(GITREV)
