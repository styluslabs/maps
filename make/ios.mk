# Makefile fragment for iOS app build - v. 2019-06-07
# use ios-deploy to deploy the resulting app
# debugging can be accomplished in Xcode by connecting to running app
# ref: https://vojtastavik.com/2018/10/15/building-ios-app-without-xcode/

# run `xcrun --sdk iphoneos --find clang` to get path to clang
# run `xcrun --sdk iphoneos --show-sdk-path` to get arg for -isysroot
XCODE = /Applications/Xcode.app/Contents/Developer
SIM ?= 0
ifneq ($(SIM), 0)
  SDKROOT = $(XCODE)/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator.sdk
  TARGET_ARCH = x86_64-apple-ios12.0-simulator
  ARCH_ONLY = x86_64
# no signing id need for sim; mobileprovision file also not needed
  CODESIGN = /usr/bin/codesign --force --sign - --timestamp=none
  XCENT = ios/Sim.app.xcent
else
  SDKROOT = $(XCODE)/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk
  TARGET_ARCH = arm64-apple-ios12.0
  ARCH_ONLY = arm64
endif
CLANG = $(XCODE)/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++ -target $(TARGET_ARCH) -isysroot $(SDKROOT)
#-arch arm64 / x86_64  -miphoneos-version-min=11.0
# note that we omit --output-partial-info-plist and assume it will not be needed
IBTOOL = XCODE_DEVELOPER_USR_PATH=$(XCODE)/usr $(XCODE)/usr/bin/ibtool --errors --warnings --notices --auto-activate-custom-fonts --target-device iphone --target-device ipad --minimum-deployment-target 12.0 --output-format human-readable-text
ACTOOL = $(XCODE)/usr/bin/actool --output-format human-readable-text --notices --warnings --app-icon AppIcon --compress-pngs --enable-on-demand-resources YES --development-region en --target-device iphone --target-device ipad --minimum-deployment-target 12.0 --platform iphoneos --product-type com.apple.product-type.application
# ar
AR = $(XCODE)/Toolchains/XcodeDefault.xctoolchain/usr/bin/libtool -static -arch_only $(ARCH_ONLY) -syslibroot $(SDKROOT)
ARFLAGS =
# strip (removes debug symbols from exe) - what Xcode uses and smaller result than '-Xlinker -S' (or -s)
STRIP = $(XCODE)/Toolchains/XcodeDefault.xctoolchain/usr/bin/strip

CFLAGS += -MMD -Wall -Werror=return-type
#-Wshadow
CXX = $(CLANG)
CXXFLAGS += --std=c++14 -fexceptions -frtti
CC = $(CLANG) -x c
CCFLAGS += --std=c99
MC = $(CLANG) -x objective-c
MFLAGS += -fobjc-arc
MXX = $(CLANG) -x objective-c++
MXXFLAGS += $(CXXFLAGS) -fobjc-arc
# linker
LD = $(CXX)
LDFLAGS +=

DEBUG ?= 1
ifneq ($(DEBUG), 0)
  CFLAGS += -O0 -g
  LDFLAGS += -rdynamic
else
  CFLAGS += -O2 -fno-omit-frame-pointer -g -DNDEBUG
  LDFLAGS += -dead_strip
endif

#ifneq ($(SIM), 0)
#  BUILDDIR := Sim$(BUILDDIR)
#endif


# project independent stuff

# assumes *FLAGS variables use deferred evaluation
CFLAGS += $(CFLAGS_PRIVATE)
CCFLAGS += $(CCFLAGS_PRIVATE)
CXXFLAGS += $(CXXFLAGS_PRIVATE)

# include files
INCFLAGS = $(INC:%=-I%) $(INC_PRIVATE:%=-I%) $(INCSYS:%=-isystem %) $(INCFILES:%=-include %)

# defines
CFLAGS += $(DEFS:%=-D%) $(DEFS_PRIVATE:%=-D%)

# frameworks
LIBS += $(FRAMEWORKS:%=-framework %)

SRCBASE=$(basename $(SOURCES))
OBJ=$(SRCBASE:%=$(OBJDIR)/%.o)
DEPS=$(SRCBASE:%=$(OBJDIR)/%.d)
TGTDIR=$(BUILDDIR)/$(APPDIR)
TGT=$(TGTDIR)/$(TARGET)
IPA=$(BUILDDIR)/$(TARGET).ipa
LIBTGT=$(BUILDDIR)/lib$(TARGET).a
# interface builder
XIBBASE=$(basename $(XIB))
NIB=$(XIBBASE:%=$(OBJDIR)/%.nib)
# gcc will not create directories, so depend on existence of all directories in output folder
# sort removes duplicates (which cause make error)
BUILDDIRS=$(sort $(dir $(OBJ))) $(TGTDIR)

.PHONY: all ipa clean distclean

all: $(TGT)

ipa: $(IPA)

lib: $(LIBTGT)

$(OBJDIR)/%.o: %.cpp
	$(CXX) -c $(CFLAGS) $(CXXFLAGS) $(INCFLAGS) -o $@ $<

$(OBJDIR)/%.o: %.cc
	$(CXX) -c $(CFLAGS) $(CXXFLAGS) $(INCFLAGS) -o $@ $<

$(OBJDIR)/%.o: %.c
	$(CC) -c $(CFLAGS) $(CCFLAGS) $(INCFLAGS) -o $@ $<

$(OBJDIR)/%.o: %.m
	$(MC) -c $(CFLAGS) $(MFLAGS) $(INCFLAGS) -o $@ $<

$(OBJDIR)/%.o: %.mm
	$(MXX) -c $(CFLAGS) $(MXXFLAGS) $(INCFLAGS) -o $@ $<

$(OBJDIR)/%.nib: %.xib
	$(IBTOOL) --module $(TARGET) --compile $@ $<

# note that you can use, e.g., ifeq($DEBUG),0) *unindented* instead of bash test
$(TGT): $(OBJ) $(IOSRES) $(NIB)
	$(LD) -o $@ $(OBJ) $(LDFLAGS) $(LIBS)
	test $(DEBUG) -ne 0 || $(STRIP) $@
	cp -R $(IOSRES) $(TGTDIR)
	$(PLISTENV) perl -pe 's{\$$\((\w+)\)}{$$ENV{$$1} // $$&}ge' < $(INFOPLIST) > $(TGTDIR)/Info.plist
	test -z "$(XCASSETS)" || $(ACTOOL) --export-dependency-info $(OBJDIR)/assetcatalog_dependencies --output-partial-info-plist $(OBJDIR)/assetcatalog_info.plist --compile $(TGTDIR) $(XCASSETS)
	test -z "$(NIB)" || cp -R $(NIB) $(TGTDIR)
	cp $(PROVISIONING_PROFILE) $(TGTDIR)/embedded.mobileprovision
	$(CODESIGN) --generate-entitlement-der --entitlements $(XCENT) $(TGTDIR)

$(IPA): $(TGT)
	(cd $(BUILDDIR) && zip -rXm $(TARGET).ipa Payload)

$(LIBTGT): $(OBJ)
	$(AR) -o $@ $^ $(ARFLAGS)

# | (pipe) operator causes make to just check for existence instead of timestamp
$(OBJ): | $(BUILDDIRS)

$(BUILDDIRS):
	mkdir -p $(BUILDDIRS)

clean:
	rm -f $(TGT) $(OBJ) $(DEPS)
	rm -r $(TGTDIR)

distclean:
	rm -rf ./Debug ./Release

# dependency files generated by gcc (-MMD switch) ("-include" ignores file if missing)
-include $(DEPS)
