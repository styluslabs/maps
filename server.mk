# Linux makefile for Ascend Maps tile server

TARGET ?= server
DEBUG ?= 1
ifneq ($(DEBUG), 0)
	BUILDDIR ?= build/Debug
else
	BUILDDIR ?= build/Release
endif

include make/shared.mk

INC += \
  tangram-es/core/deps/glm \
	tangram-es/core/deps/stb

DEFS += GLM_FORCE_CTOR_INIT

## sqlite
MODULE_BASE = tangram-es/core/deps/sqlite3

MODULE_SOURCES = sqlite3.c
MODULE_INC_PUBLIC = .
MODULE_DEFS_PRIVATE = SQLITE_ENABLE_FTS5
MODULE_DEFS_PUBLIC = SQLITE_USE_URI=1

include $(ADD_MODULE)

## server
MODULE_BASE := .

MODULE_SOURCES = \
  scripts/server.cpp \
	tangram-es/core/src/util/mapProjection.cpp

MODULE_INC_PRIVATE = $(STYLUSLABS_DEPS) tangram-es/core/include/tangram tangram-es/core/src
#MODULE_DEFS_PRIVATE = PUGIXML_NO_XPATH PUGIXML_NO_EXCEPTIONS SVGGUI_NO_SDL

include $(ADD_MODULE)

#LIBS = -pthread

include make/unix.mk