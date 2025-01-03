# cross-platform C/C++ makefile for Ascend Maps

TARGET = ascend

DEBUG ?= 0
ifneq ($(DEBUG), 0)
  BUILDTYPE = Debug
else
  BUILDTYPE = Release
endif

ifneq ($(ANDROID),)
  BUILDDIR ?= build/Android$(BUILDTYPE)
else
  BUILDDIR ?= build/$(BUILDTYPE)
endif

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

## common modules
include module.mk
include tangram-es/core/module.mk


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

include make/msvc.mk

else ifneq ($(XPC_FLAGS),)
# iOS (XPC_FLAGS seems to be defined on macOS)

SOURCES += ios/ioshelper.m
DEFS += GLES_SILENCE_DEPRECATION

include make/ios.mk

else ifneq ($(ANDROID),)
# ./gww assembleRelease && cp app/build/outputs/apk/release/app-release.apk . && ./resignapk.sh app-release.apk ~/styluslabs.keystore && mv signed_app-release.apk ascend.apk

# System.loadLibrary("droidmaps") in MapsLib.java
TARGET = libdroidmaps.so
ANDROID_NDK = $(HOME)/android-sdk/ndk/26.3.11579264

# platform
MODULE_BASE := .

MODULE_SOURCES += \
  app/android/app/src/main/cpp/androidApp.cpp \
  tangram-es/platforms/common/platform_gl.cpp \
  tangram-es/platforms/android/tangram/src/main/cpp/JniHelpers.cpp \
  tangram-es/platforms/android/tangram/src/main/cpp/AndroidPlatform.cpp \
  tangram-es/platforms/android/tangram/src/main/cpp/sqlite_fdvfs.c \
  tangram-es/platforms/android/tangram/src/main/cpp/sqlite3ndk.cpp

MODULE_INC_PRIVATE = app/include tangram-es/platforms/android/tangram/src/main/cpp $(STYLUSLABS_DEPS)
MODULE_DEFS_PRIVATE = SVGGUI_NO_SDL

include $(ADD_MODULE)

DEFS += TANGRAM_ANDROID
LIBS = android atomic GLESv3 EGL log z jnigraphics

include make/android.mk

else
# Linux
# GLFW
include tangram-es/platforms/common/module.mk

# Linux platfrom
MODULE_BASE := .

MODULE_SOURCES += \
  tangram-es/platforms/linux/src/linuxPlatform.cpp \
  tangram-es/platforms/common/platform_gl.cpp \
  tangram-es/platforms/common/urlClient.cpp \
  tangram-es/platforms/common/linuxSystemFontHelper.cpp \
  deps/nfd/src/nfd_portal.cpp \
  app/src/glfwmain.cpp \
  $(STYLUSLABS_DEPS)/ugui/example/glfwSDL.c

MODULE_INC_PRIVATE = app/include tangram-es/platforms/common deps/nfd/src/include $(STYLUSLABS_DEPS)
MODULE_DEFS_PRIVATE = SVGGUI_NO_SDL

include $(ADD_MODULE)

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

include make/unix.mk

endif

.DEFAULT_GOAL := all
