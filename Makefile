# cut and paste from tangram-es Makefile

all: linux

.PHONY: clean-linux
.PHONY: linux
.PHONY: cmake-linux

# Default build type is Release
BUILD_TYPE ?= Release

LINUX_BUILD_DIR = build/${BUILD_TYPE}

LINUX_CMAKE_PARAMS = \
	-DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
	-DTANGRAM_PLATFORM=linux \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=TRUE \
	${CMAKE_OPTIONS}

clean-linux:
	rm -rf ${LINUX_BUILD_DIR}

linux: cmake-linux
	cmake --build ${LINUX_BUILD_DIR} ${CMAKE_BUILD_OPTIONS}

cmake-linux:
	cmake -H. -B${LINUX_BUILD_DIR} ${LINUX_CMAKE_PARAMS}
