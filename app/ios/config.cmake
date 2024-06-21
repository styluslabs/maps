add_definitions(-DTANGRAM_IOS)
add_compile_options(-g)  # always include debug info

if(TANGRAM_IOS_SIM)
  add_compile_options(--target=x86_64-apple-ios12.0-simulator)
  add_link_options(--target=x86_64-apple-ios12.0-simulator)
else()
  add_compile_options(--target=arm64-apple-ios12.0)
  add_link_options(--target=arm64-apple-ios12.0)
endif()

# seems to be the only way to get cmake to combine all the static libs; we'll be switching to plain makefiles soon
#set(CMAKE_CXX_LINK_EXECUTABLE "<CMAKE_AR> rcT libmaps-ios.a <OBJECTS> <LINK_LIBRARIES>") -- doesn't work

#set(TANGRAM_FRAMEWORK_VERSION "0.17.2-dev")
#set(TANGRAM_BUNDLE_IDENTIFIER "com.mapzen.TangramMap")

### Configure iOS toolchain.
#set(CMAKE_OSX_DEPLOYMENT_TARGET "9.3") # Applies to iOS even though the variable name says OSX.
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden -fvisibility-inlines-hidden")
#execute_process(COMMAND xcrun --sdk iphoneos --show-sdk-version OUTPUT_VARIABLE IOS_SDK_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
# Set the global BITCODE_GENERATION_MODE value to 'bitcode' for Release builds.
# This is for generating full bitcode outside of the "archive" command.
# Set 'marker' for other configs to make debug builds install-able on devices.
#set(CMAKE_XCODE_ATTRIBUTE_BITCODE_GENERATION_MODE "$<IF:$<CONFIG:Release>,bitcode,marker>")

enable_language(OBJC)
enable_language(OBJCXX)
set(CMAKE_OBJCXX_FLAGS "${CMAKE_OBJCXX_FLAGS} -fobjc-arc")
set(CMAKE_OBJC_FLAGS "${CMAKE_OBJC_FLAGS} -fobjc-arc")

set(TANGRAM_FRAMEWORK_SOURCES
  tangram-es/platforms/common/appleAllowedFonts.h
  tangram-es/platforms/common/appleAllowedFonts.mm
  tangram-es/platforms/common/platform_gl.cpp
  tangram-es/platforms/ios/framework/src/iosPlatform.h
  tangram-es/platforms/ios/framework/src/iosPlatform.mm
  tangram-es/platforms/ios/framework/src/TGURLHandler.mm
  app/ios/AppDelegate.m
  app/ios/OpenGLView.m
  app/ios/GLViewController.mm
  app/ios/iosApp.cpp
)

### Configure static library build target.

add_executable(maps-ios
  ${TANGRAM_FRAMEWORK_SOURCES}
)
# partial linking to produce an object file instead of an executable
target_link_options(maps-ios PRIVATE "-r" "-nodefaultlibs")

target_include_directories(maps-ios PRIVATE
  tangram-es/platforms/common
  tangram-es/platforms/ios/framework/src
  tangram-es/core/deps/stb
  tangram-es/core/deps/yaml-cpp/include
  ${STYLUSLABS_DEPS}
)

target_compile_definitions(maps-ios PRIVATE GLES_SILENCE_DEPRECATION)

target_link_libraries(maps-ios PRIVATE
  maps-app
  tangram-core
)

# tangram won't add duktape if using JSCore
if (TANGRAM_USE_JSCORE)
  target_link_libraries(maps-ios PRIVATE duktape)
endif()

# Set properties common between dynamic and static framework targets.
#set_target_properties(tangram-static PROPERTIES
#  #XCODE_GENERATE_SCHEME TRUE
#  #XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "${TANGRAM_FRAMEWORK_VERSION}"
#  XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
#  XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD "c++14"
#  #XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++"
#  #XCODE_ATTRIBUTE_GCC_TREAT_WARNINGS_AS_ERRORS "YES"
#  # Generate dsym for all build types to ensure symbols are available in profiling.
#  XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS "YES"
#  # Ensure that archives are built for distribution.
#  #XCODE_ATTRIBUTE_BUILD_LIBRARY_FOR_DISTRIBUTION "YES"
#)
