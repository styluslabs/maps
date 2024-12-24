# cross-platform C/C++ makefile for Ascend Maps

TARGET = ascend

include make/shared.mk

ifneq ($(wildcard styluslabs/.),)
  STYLUSLABS_DEPS=styluslabs
else
  STYLUSLABS_DEPS=deps
endif

ifneq ($(DEBUG),0)
  DEFS += LOG_LEVEL=3
else
  DEFS += LOG_LEVEL=2
endif

## modules
include module.mk
include tangram-es/core/module.mk
include tangram-es/platforms/common/glfw/module.mk


## platform
MODULE_BASE := .

ifneq ($(windir),)
# Windows

SOURCES += \
  windows/winhelper.cpp \
  windows/wintab/Utils.c \
  ../nanovg-2/glad/glad.c

RESOURCES = windows/resources.rc
INCSYS += ../SDL/include
DEFS += _USE_MATH_DEFINES UNICODE NOMINMAX FONS_WPATH

# only dependencies under this path will be tracked in .d files; note [\\] must be used for "\"
# ensure that no paths containing spaces are included
DEPENDBASE ?= c:[\\]temp[\\]maps

# shell32 for ShellExecute; user32 for clipboard fns; libs below opengl32.lib needed only for static SDL
LIBS = \
  ws2_32.lib \
  shell32.lib \
  user32.lib \
  glu32.lib \
  opengl32.lib \
  gdi32.lib \
  winmm.lib \
  ole32.lib \
  oleaut32.lib \
  advapi32.lib \
  setupapi.lib \
  imm32.lib \
  version.lib

# distribution package
ZIPFILE = write$(HGREV).zip
ZIPDIR = Write
DISTRES = \
  ../scribbleres/fonts/Roboto-Regular.ttf \
  ../scribbleres/fonts/DroidSansFallback.ttf \
  ../scribbleres/Intro.svg
# installer
WXS = windows/InstallWrite.wxs

PLATFORM_MK = make/msvc.mk

else ifneq ($(XPC_FLAGS),)
# iOS (XPC_FLAGS seems to be defined on macOS)

SOURCES += ios/ioshelper.m
DEFS += GLES_SILENCE_DEPRECATION

include make/ios.mk

else ifneq ($(BUILD_SHARED_LIBRARY),)
# Android
# ./start_gradle assembleRelease && cp app/build/outputs/apk/release/app-release.apk . && ./resignapk.sh app-release.apk ~/styluslabs.keystore && mv signed_app-release.apk write300.apk

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

SOURCES += android/androidhelper.cpp

# System.loadLibrary("droidmaps") in MapsLib.java
LOCAL_MODULE := droidmaps

ALL_INC := $(INC) $(INCSYS)
LOCAL_C_INCLUDES := $(addprefix $(LOCAL_PATH)/, $(ALL_INC))

LOCAL_CFLAGS := $(addprefix -D, $(DEFS))
LOCAL_CPPFLAGS := -std=c++14 -Wno-unused -Wno-error=format-security

LOCAL_SRC_FILES := $(addprefix $(LOCAL_PATH)/, $(SOURCES))
#LOCAL_SHARED_LIBRARIES := SDL2
# libandroid needed for ANativeWindow_* fns
LOCAL_LDLIBS := -lGLESv3 -llog -ljnigraphics -landroid

include $(BUILD_SHARED_LIBRARY)

else
# Linux

MODULE_SOURCES += \
  tangram-es/platforms/linux/src/linuxPlatform.cpp \
  tangram-es/platforms/common/platform_gl.cpp \
  tangram-es/platforms/common/urlClient.cpp \
  tangram-es/platforms/common/linuxSystemFontHelper.cpp \
  deps/nfd/src/nfd_portal.cpp \
  app/src/glfwmain.cpp \
  $(STYLUSLABS_DEPS)/ugui/example/glfwSDL.c

MODULE_INC_PRIVATE = $(STYLUSLABS_DEPS) $(STYLUSLABS_DEPS)/nanovgXC/src $(STYLUSLABS_DEPS)/pugixml/src deps/nfd/src/include tangram-es/platforms/common app/include
MODULE_DEFS_PRIVATE = PUGIXML_NO_XPATH PUGIXML_NO_EXCEPTIONS SVGGUI_NO_SDL

PKGS = dbus-1 x11

DEFS += TANGRAM_LINUX
LIBS = -pthread -lOpenGL -lfontconfig -lcurl
#CFLAGS = -pthread

# distribution package
GITREV := $(shell git rev-parse --short HEAD)
TGZ = $(TARGET)-$(GITREV).tar.gz
DISTRES = \
  assets/config.default.yaml \
  assets/mapsources.default.yaml \
  assets/plugins \
  assets/res \
  assets/scenes \
  assets/shared \
  app/linux/setup \
  app/linux/INSTALL

PLATFORM_MK = make/unix.mk

endif

# platform sources
include $(ADD_MODULE)

include $(PLATFORM_MK)
.DEFAULT_GOAL := all
