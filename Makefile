# cross-platform C/C++ makefile for Ascend Maps

TARGET = ascend

DEBUG ?= 0
ifneq ($(DEBUG), 0)
  BUILDTYPE = Debug
else
  BUILDTYPE = Release
endif

# don't use backslash in Windows BUILDDIR, breaks make dependencies
ifneq ($(ANDROID),)
  BUILDDIR ?= build/Android$(BUILDTYPE)
else
  BUILDDIR ?= build/$(BUILDTYPE)
endif

include make/shared.mk

ifneq ($(DEBUG),0)
  DEFS += LOG_LEVEL=3
else
  DEFS += LOG_LEVEL=2
  #DEFS += GLM_FORCE_INTRINSICS
endif

# if deferred eval, make sure to only use for source that needs GIT_REV
# refs: https://stackoverflow.com/questions/17097263
GIT_REV := $(shell git rev-parse --short HEAD || wsl git rev-parse --short HEAD)
GIT_TAGCOUNT := $(shell git rev-list --count --tags --no-walk || wsl git rev-list --count --tags --no-walk)
GIT_DESCRIBE := $(shell git describe --tags --dirty || wsl git describe --tags --dirty)
## common modules
include tangram-es/core/module.mk
include module.mk


ifeq ($(OS),Windows_NT)
# Windows
CURL_BASE := ../curl

MODULE_BASE := .

MODULE_SOURCES += \
  app/windows/winmain.cpp \
  tangram-es/platforms/windows/src/windowsPlatform.cpp \
  tangram-es/platforms/common/platform_gl.cpp \
  tangram-es/platforms/common/urlClient.cpp \
  deps/nfd/src/nfd_win.cpp \
  deps/nanovgXC/glad/glad.c \
  deps/nanovgXC/glad/glad_wgl.c

MODULE_INC_PUBLIC = $(STYLUSLABS_DEPS)/nanovgXC
MODULE_INC_PRIVATE = app/include tangram-es/platforms/common tangram-es/platforms/windows/src $(STYLUSLABS_DEPS) $(CURL_BASE)/include deps/nfd/src/include
MODULE_DEFS_PRIVATE = SVGGUI_NO_SDL

include $(ADD_MODULE)

RESOURCES = app/windows/resources.rc
RCFLAGS = /DVERSIONSTR=\"$(GIT_DESCRIBE)\" /DVERSIONCSV="1,0,$(GIT_TAGCOUNT),0"
#/DVERSIONSTR=\"$(MAJORVER).$(MINORVER).$(GITTAGCOUNT)\" /DVERSIONCSV="$(MAJORVER),$(MINORVER),$(GITTAGCOUNT),0"
#INCSYS += ../SDL/include
DEFS += TANGRAM_WINDOWS _USE_MATH_DEFINES UNICODE NOMINMAX FONS_WPATH

# only dependencies under this path will be tracked in .d files; note [\\] must be used for "\"
# ensure that no paths containing spaces are included
DEPENDBASE ?= $(subst /,[\\],$(CURDIR))

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
  version.lib \
  $(CURL_BASE)\build\lib\Release\libcurl_imp.lib

# distribution package
ZIPFILE = $(TARGET)-$(GIT_DESCRIBE)-$(GIT_REV).zip
ZIPDIR = Ascend
DISTRES = \
  assets\config.default.yaml \
  assets\mapsources.default.yaml \
  assets\plugins \
  assets\res \
  assets\scenes \
  assets\shared \
  $(CURL_BASE)\build\lib\Release\libcurl.dll
# installer
#WXS = windows/InstallWrite.wxs

include make/msvc.mk

else ifneq ($(XPC_FLAGS),)
# iOS (XPC_FLAGS seems to be defined on macOS)

MODULE_BASE := .

MODULE_SOURCES += \
  tangram-es/platforms/common/appleAllowedFonts.mm \
  tangram-es/platforms/common/platform_gl.cpp \
  tangram-es/platforms/ios/framework/src/iosPlatform.mm \
  tangram-es/platforms/ios/framework/src/TGURLHandler.mm \
  app/ios/AppDelegate.m \
  app/ios/OpenGLView.m \
  app/ios/GLViewController.mm \
  app/ios/iosApp.cpp

MODULE_INC_PRIVATE = app/include tangram-es/platforms/common tangram-es/platforms/ios/framework/src $(STYLUSLABS_DEPS)
MODULE_DEFS_PRIVATE = SVGGUI_NO_SDL

include $(ADD_MODULE)

DEFS += TANGRAM_IOS GLES_SILENCE_DEPRECATION

## For now, we'll only support building lib w/ make and let Xcode build the final app
#XIB = ios/LaunchView.xib
## app store now requires app icon be in an asset catalog
#XCASSETS = ../scribbleres/Assets.xcassets
#IOSRES = \
#  ../scribbleres/fonts/SanFranciscoDisplay-Regular.otf \
#  ../scribbleres/fonts/DroidSansFallback.ttf \
#  ../scribbleres/Intro.svg
#INFOPLIST = ios/Info.plist
#PLISTENV = CURRENT_PROJECT_VERSION=1.5 MARKETING_VERSION=1.5
#
#DIST ?= 0
#ifeq ($(DIST), 0)
#  APPDIR = Write.app
#  PROVISIONING_PROFILE = /Users/mwhite/Documents/mwhite_iOS_Dev_2024.mobileprovision
#  XCENT = ios/Dev.app.xcent
#  CODESIGN = /usr/bin/codesign --force --sign 9E6635D070FC516CD66467812DDFA3CFDD010E3D --timestamp=none
#else
#  ifneq ($(DEBUG), 0)
#    $(error DIST build requires DEBUG=0)
#  endif
#  APPDIR = Payload/Write.app
#  ifeq ("$(DIST)", "appstore")
#    PROVISIONING_PROFILE = /Users/mwhite/Documents/Stylus_Labs_App_Store_2024.mobileprovision
#    XCENT = ios/AppStore.app.xcent
#  else
#    PROVISIONING_PROFILE = /Users/mwhite/Documents/Stylus_Labs_AdHoc_2020.mobileprovision
#    XCENT = ios/AdHoc.app.xcent
#  endif
#  CODESIGN = /usr/bin/codesign --force --sign 55648202533A2234B7ED12254A2C271BC52ACED187E0 --timestamp=none
#endif

#FRAMEWORKS = AVFoundation GameController CoreMotion Foundation UIKit CoreGraphics OpenGLES QuartzCore CoreAudio AudioToolbox Metal StoreKit

include make/ios.mk

else ifneq ($(ANDROID),)
# ./gww assembleRelease && cp app/build/outputs/apk/release/app-release.apk . && ./resignapk.sh app-release.apk ~/styluslabs.jks && mv signed_app-release.apk ascend.apk

# System.loadLibrary("droidmaps") in MapsLib.java
TARGET = libdroidmaps.so
ANDROID_NDK_HOME ?= $(lastword $(sort $(wildcard $(ANDROID_HOME)/ndk/*)))

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
#include tangram-es/platforms/common/module.mk

# Linux platfrom
MODULE_BASE := .

MODULE_SOURCES += \
  tangram-es/platforms/linux/src/linuxPlatform.cpp \
  tangram-es/platforms/common/platform_gl.cpp \
  tangram-es/platforms/common/urlClient.cpp \
  tangram-es/platforms/common/linuxSystemFontHelper.cpp \
  deps/nfd/src/nfd_portal.cpp \
  app/linux/linuxmain.cpp

MODULE_INC_PUBLIC = tangram-es/platforms/linux/src
MODULE_INC_PRIVATE = app/include tangram-es/platforms/common deps/nfd/src/include $(STYLUSLABS_DEPS) $(STYLUSLABS_DEPS)/nanovgXC
MODULE_DEFS_PRIVATE = SVGGUI_NO_SDL

include $(ADD_MODULE)

PKGS = dbus-1 x11

DEFS += TANGRAM_LINUX
LIBS = -pthread -lOpenGL -lGLX -lXi -lfontconfig -lcurl -ldl
#CFLAGS = -pthread

PROFILE ?= 0
ifneq ($(PROFILE),0)
  DEFS += TANGRAM_JS_TRACING=1
endif

# distribution package
TGZ_FOLDER = Ascend
TGZ = $(TARGET)-$(GIT_DESCRIBE)-$(GIT_REV).tar.gz
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
