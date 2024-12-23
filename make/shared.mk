DEBUG ?= 0
ifneq ($(DEBUG), 0)
  BUILDDIR ?= Debug
else
  BUILDDIR ?= Release
endif

# if source contains ../ paths, this should be the <current> directory; if ../../, <parent>/<current>; etc.
# - this ensures object files remain under build directory
#~TOPDIR = syncscribble

ifneq ($(TOPDIR),)
  OBJDIR=$(BUILDDIR)/$(TOPDIR)
else
  OBJDIR=$(BUILDDIR)
endif

# define as immediately evaluated so appending for modules works
SOURCES :=
INC :=
DEFS :=
INCSYS =
INC_PRIVATE =

## module functions
GET_MAKE_BASE = $(patsubst %/module.mk,%,$(lastword $(MAKEFILE_LIST)))
ADD_MODULE = make/add_module.mk
