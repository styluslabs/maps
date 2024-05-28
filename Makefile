# cut and paste from tangram-es Makefile

.PHONY: all clean-linux linux cmake-linux tgz

# Default build type is Release
BUILD_TYPE ?= Release

LINUX_BUILD_DIR = build/${BUILD_TYPE}

LINUX_CMAKE_PARAMS = \
	-DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
	-DTANGRAM_PLATFORM=linux \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=TRUE \
	${CMAKE_OPTIONS}

GITREV := $(shell git rev-parse --short HEAD)
TGZ = maps-$(GITREV).tar.gz
TGT = $(LINUX_BUILD_DIR)/tangram

DISTRES = \
  ../scribbleres/fonts/Roboto-Regular.ttf \
  ../scribbleres/fonts/DroidSansFallback.ttf \
  ../scribbleres/Intro.svg \
  ../scribbleres/linux/Write.desktop \
  ../scribbleres/linux/Write144x144.png \
  ../scribbleres/linux/setup.sh \
  ../scribbleres/linux/INSTALL

assets/config.default.yaml \
assets/mapsources.default.yaml \
assets/plugins/ \
assets/res/ \
assets/scenes/


# targets
all: linux

clean-linux:
	rm -rf ${LINUX_BUILD_DIR}

linux: cmake-linux
	cmake --build ${LINUX_BUILD_DIR} ${CMAKE_BUILD_OPTIONS}

cmake-linux:
	cmake -H. -B${LINUX_BUILD_DIR} ${LINUX_CMAKE_PARAMS}

tgz: $(TGZ)

$(TGZ): linux
	strings $(TGT) | grep "^GLIBC_"
	mkdir -p $(LINUX_BUILD_DIR)/.dist
	mv $(TGT) $(LINUX_BUILD_DIR)/.dist
	cp -R $(DISTRES) $(LINUX_BUILD_DIR)/.dist
	(cd $(LINUX_BUILD_DIR) && mv .dist $(TARGET) && tar --remove-files -czvf $@ $(TARGET))
