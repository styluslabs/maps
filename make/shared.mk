# if source contains ../ paths, this should be the <current> directory; if ../../, <parent>/<current>; etc.
# - this ensures object files remain under build directory
#~TOPDIR = syncscribble

ifneq ($(TOPDIR),)
  OBJDIR=$(BUILDDIR)/$(TOPDIR)
else
  OBJDIR=$(BUILDDIR)
endif

# release package goals: sources w/ version info are always rebuilt (FORCE prerequisite, see module.mk), so
#  package never has stale version info; unlike touching the source, this is safe w/ -j and doesn't modify files
PACKAGE_GOALS = tgz real_tgz zip msi ipa
PACKAGE_BUILD = $(filter $(PACKAGE_GOALS),$(MAKECMDGOALS))

ifeq ($(OS),Windows_NT)
  OBJEXT=obj
else
  OBJEXT=o
endif

ifneq ($(wildcard styluslabs/.),)
  STYLUSLABS_DEPS=styluslabs
else
  STYLUSLABS_DEPS=deps
endif

# define as immediately evaluated so appending for modules works
SOURCES :=
INC :=
DEFS :=
INCSYS =
INC_PRIVATE =
GENERATED =

## module functions
GET_MAKE_BASE = $(patsubst %/module.mk,%,$(lastword $(MAKEFILE_LIST)))
ADD_MODULE = make/add_module.mk
