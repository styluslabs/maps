# Linux makefile for Ascend Maps tile server

TARGET ?= server
DEBUG ?= 0
SERVER ?= 1
ifneq ($(DEBUG), 0)
  BUILDDIR ?= build/Debug
else
  BUILDDIR ?= build/Release
  CFLAGS += -march=native
  LDFLAGS += -march=native
endif

include make/shared.mk

## sqlite
MODULE_BASE = tangram-es/core/deps/sqlite3

MODULE_SOURCES = sqlite3.c
MODULE_INC_PUBLIC = .
MODULE_DEFS_PRIVATE = SQLITE_ENABLE_FTS5
MODULE_DEFS_PUBLIC = SQLITE_USE_URI=1

include $(ADD_MODULE)

## geodesk
MODULE_BASE = deps/libgeodesk

rwildcard=$(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))
MODULE_FULL_SOURCES = $(call rwildcard,$(MODULE_BASE)/src,*.cpp)
#MODULE_FULL_SOURCES = $(wildcard $(MODULE_BASE)/src/*/*.cpp)
MODULE_INC_PUBLIC = include src

MODULE_CXXFLAGS = --std=c++20 -Wno-unknown-pragmas -Wno-reorder

include $(ADD_MODULE)

## server
MODULE_BASE := .

MODULE_SOURCES = \
  tangram-es/core/deps/miniz/miniz.c \
  scripts/tilebuilder.cpp \
  scripts/ascendtiles.cpp

ifneq ($(SERVER), 0)
  MODULE_SOURCES += scripts/server.cpp
else
  DEFS += ASCENDTILES_MAIN
endif

MODULE_INC_PRIVATE = \
  $(STYLUSLABS_DEPS) \
  deps/vtzero/include \
  deps/protozero/include

#MODULE_DEFS_PRIVATE = PUGIXML_NO_XPATH PUGIXML_NO_EXCEPTIONS SVGGUI_NO_SDL

MODULE_CXXFLAGS = --std=c++20 -Wno-unknown-pragmas -Wno-reorder

include $(ADD_MODULE)

#$(info server: $(SOURCES))
include make/unix.mk
