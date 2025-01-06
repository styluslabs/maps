# Linux makefile for Ascend Maps tests

TARGET ?= tests.out
DEBUG ?= 1
BUILDDIR ?= build/Debug

include make/shared.mk

# only needed for fontstash.h for mockPlatform.cpp
ifneq ($(wildcard styluslabs/.),)
  STYLUSLABS_DEPS=styluslabs
else
  STYLUSLABS_DEPS=deps
endif

DEFS += LOG_LEVEL=3

## modules
include tangram-es/core/module.mk
include tangram-es/tests/module.mk

LIBS = -pthread -lOpenGL -lfontconfig -lcurl
DEFS += TANGRAM_LINUX

include make/unix.mk

.DEFAULT_GOAL := run

.PHONY: run

run: all
	$(BUILDDIR)/$(TARGET)


# Old ideas for combining with main Makefile
#
# # with $(SOURCES_VAR) = ... in add_module.mk; otherwise, TANGRAM_SOURCES := SOURCES \n SOURCES = ""
# SOURCES_VAR=TANGRAM_SOURCES
# include tangram-es/core/module.mk
#
# SOURCES_VAR=TANGRAM_TEST_SOURCES
# include tangram-es/tests/module.mk
#
# SOURCES_VAR=SOURCES
# ...
#
#
# TEST_SOURCES = $(TANGRAM_SOURCES) $(TANGRAM_TEST_SOURCES)
#
# SOURCES = $(TANGRAM_SOURCES) $(PLATFORM_SOURCES) $(APP_SOURCES)
#
# # for deps
# ALL_SOURCES = $(TANGRAM_SOURCES) $(TANGRAM_TEST_SOURCES) $(PLATFORM_SOURCES) $(APP_SOURCES)
#
#
# OBJ=$($(basename $(SOURCES)):%=$(OBJDIR)/%.o)
# TEST_OBJ=$($(basename $(TEST_SOURCES)):%=$(OBJDIR)/%.o)
# DEPS=$($(basename $(ALL_SOURCES)):%=$(OBJDIR)/%.d)
#
#
# all: $(TGT)
#
# tests: $(TESTS_TGT)
#
# TGT=$(BUILDDIR)/$(TARGET)
# TEST_TGT=$(BUILDDIR)/$(TEST_TARGET)
#
# $(TGT): $(OBJ)
# 	$(LD) -o $@ $^ $(LDFLAGS) $(LIBS)
#
# $(TESTS_TGT): $(TEST_OBJ)
# 	$(LD) -o $@ $^ $(LDFLAGS) $(LIBS)
#
#
# LINK_CMD=$(LD) -o $@ $^ $(LDFLAGS) $(LIBS)
#
# all: $(BUILDDIR)/$(TARGET)
# $(BUILDDIR)/$(TARGET): $($(basename $(SOURCES)):%=$(OBJDIR)/%.o)
# 	$(LINK_CMD)
#
# tests: $(BUILDDIR)/$(TEST_TARGET)
# $(BUILDDIR)/$(TEST_TARGET): $($(basename $(TEST_SOURCES)):%=$(OBJDIR)/%.o)
# 	$(LINK_CMD)
#
