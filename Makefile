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
TGZ = explore-$(GITREV).tar.gz
TGT = $(LINUX_BUILD_DIR)/explore

DISTRES = \
	assets/config.default.yaml \
	assets/mapsources.default.yaml \
	assets/plugins/ \
	assets/res/ \
	assets/scenes/ \
	assets/shared/ \
	app/linux/install/ \
	app/linux/INSTALL

# targets
all: linux

clean-linux:
	rm -rf ${LINUX_BUILD_DIR}

linux: cmake-linux
	cmake --build ${LINUX_BUILD_DIR} ${CMAKE_BUILD_OPTIONS}

cmake-linux:
	cmake -H. -B${LINUX_BUILD_DIR} ${LINUX_CMAKE_PARAMS}

tgz: linux $(DISTRES)
	strings $(TGT) | grep "^GLIBC_"
	mkdir -p $(LINUX_BUILD_DIR)/.dist
	mv $(TGT) $(LINUX_BUILD_DIR)/.dist
	cp -R $(DISTRES) $(LINUX_BUILD_DIR)/.dist
	(cd $(LINUX_BUILD_DIR) && mv .dist Explore && tar --remove-files -czvf $(TGZ) Explore)
