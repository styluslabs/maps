# cut and paste from tangram-es Makefile

.PHONY: all clean-linux linux cmake-linux tgz clean-ios ios cmake-ios

# Default build type is Release
BUILD_TYPE ?= Release

LINUX_BUILD_DIR = build/${BUILD_TYPE}
IOS_BUILD_DIR = build/${BUILD_TYPE}
IOS_SIM_BUILD_DIR = build/Sim${BUILD_TYPE}

LINUX_CMAKE_PARAMS = \
	-DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
	-DTANGRAM_PLATFORM=linux \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=TRUE \
	${CMAKE_OPTIONS}

IOS_CMAKE_PARAMS = \
	-DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
	-DTANGRAM_PLATFORM=ios \
	-DCMAKE_SYSTEM_NAME=iOS \
	-DTANGRAM_IOS=ON \
	-DTANGRAM_USE_JSCORE=OFF \
	${CMAKE_OPTIONS}
#	-G Xcode \
#	-DCMAKE_XCODE_GENERATE_SCHEME=0 \
# -- these seems to give arm64 Mac not iOS - need to use --target=arm64-apple-ios12.0
#	-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
#	-DCMAKE_OSX_ARCHITECTURES=arm64 \

GITREV := $(shell git rev-parse --short HEAD)
TGZ = ascend-$(GITREV).tar.gz
TGT = $(LINUX_BUILD_DIR)/ascend

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
	cp -R -L $(DISTRES) $(LINUX_BUILD_DIR)/.dist
	(cd $(LINUX_BUILD_DIR) && mv .dist Ascend && tar --remove-files -czvf $(TGZ) Ascend)

clean-ios:
	rm -rf ${IOS_BUILD_DIR}

ios: cmake-ios
	cmake --build ${IOS_BUILD_DIR} ${CMAKE_BUILD_OPTIONS}

cmake-ios:
	cmake -H. -B${IOS_BUILD_DIR} ${IOS_CMAKE_PARAMS}

ios-sim: cmake-ios-sim
	cmake --build ${IOS_SIM_BUILD_DIR} ${CMAKE_BUILD_OPTIONS}

cmake-ios-sim:
	cmake -H. -B${IOS_SIM_BUILD_DIR} ${IOS_CMAKE_PARAMS} -DTANGRAM_IOS_SIM=ON -DCMAKE_OSX_SYSROOT=/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator.sdk
