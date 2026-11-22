## add_module.mk

#$(info add_module: $(MODULE_BASE))

MODULE_FULL_SOURCES += $(MODULE_SOURCES:%=$(MODULE_BASE)/%)
SOURCES += $(MODULE_FULL_SOURCES)
# "dir/." -> "dir": otherwise gcc writes "dir/./file.h" in .d files, which make treats as a different file from
#  "dir/file.h" (e.g., target of a download rule), so dependents aren't rebuilt when it changes
INC += $(patsubst %/.,%,$(MODULE_INC_PUBLIC:%=$(MODULE_BASE)/%))
DEFS += $(MODULE_DEFS_PUBLIC)

# MODULE_OBJS must be immediate to be used after clearing MODULE_FULL_SOURCES (it is immediate anyway in rules)
MODULE_SRCBASE = $(basename $(MODULE_FULL_SOURCES))
MODULE_OBJS := $(MODULE_SRCBASE:%=$(OBJDIR)/%.$(OBJEXT))

#$(info add_module: $(MODULE_BASE) MODULE_OBJS: $(MODULE_OBJS) TEMP_INC_PRIVATE: $(TEMP_INC_PRIVATE))

$(MODULE_OBJS): INC_PRIVATE := $(patsubst %/.,%,$(MODULE_INC_PRIVATE:%=$(MODULE_BASE)/%))
$(MODULE_OBJS): DEFS_PRIVATE := $(MODULE_DEFS_PRIVATE)
$(MODULE_OBJS): CFLAGS_PRIVATE := $(MODULE_CFLAGS)
$(MODULE_OBJS): CCFLAGS_PRIVATE := $(MODULE_CCFLAGS)
$(MODULE_OBJS): CXXFLAGS_PRIVATE := $(MODULE_CXXFLAGS)

# clear variables to allow multiple modules in one file
MODULE_SOURCES =
MODULE_FULL_SOURCES =
MODULE_INC_PUBLIC =
MODULE_INC_PRIVATE =
MODULE_DEFS_PUBLIC =
MODULE_DEFS_PRIVATE =
MODULE_CFLAGS =
MODULE_CCFLAGS =
MODULE_CXXFLAGS =
