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

# Lists appended to by modules are immediately evaluated (:=), since appended values reference variables like
#  MODULE_BASE which later modules change.  *FLAGS (and INCFLAGS) must use deferred evaluation (=), since they
#  reference per-object values (CFLAGS_PRIVATE, INC_PRIVATE, etc., set in add_module.mk) which are only
#  resolved when expanded in a compile command; capture $(shell ...) results w/ := before adding to *FLAGS so
#  the command doesn't run for every object.
SOURCES :=
INC :=
DEFS :=
INCSYS :=
INC_PRIVATE :=
GENERATED :=

## module functions
GET_MAKE_BASE = $(patsubst %/module.mk,%,$(lastword $(MAKEFILE_LIST)))
ADD_MODULE = make/add_module.mk


## downloads
# $(call FETCH_FILES,<dir>,<url>,<files>): download archive (.zip, .tar.gz, .tgz, etc.) from <url> and extract
#  <files> (paths in archive, all at the same depth) into <dir> (w/o their archive dirs).  Extracted files are
#  added to GENERATED, so they exist before anything is compiled - caller doesn't need to add them.  Download is
#  repeated if archive name in <url> changes (stamp file <dir>/.fetch-<first file>-<archive name>), so <url>
#  should include version, or if any of <files> is missing.  Stamp is also added to GENERATED (for distclean).
COMMA := ,
FETCH_FILES = $(eval $(call FETCH_FILES_RULES,$(1),$(2),$(3),.fetch-$(notdir $(firstword $(3))),$(1)/.fetch-$(notdir $(firstword $(3)))-$(notdir $(2)),$(addprefix $(1)/,$(notdir $(3)))))
# number of leading dirs to strip from <files>
FETCH_STRIP = $(words $(wordlist 2,99,$(subst /, ,$(firstword $(1)))))

# args: <dir>,<url>,<files>,<stamp/temp file prefix>,<stamp>
ifeq ($(OS),Windows_NT)
# tar is bsdtar (Windows 10+), which reads zip and compressed tar; copy /b +,, updates mtime (archive preserves
#  original mtimes)
FETCH_FILES_CMDS = \
  cd $(1) && curl -fL "$(2)" -o $(4).tmp && \
  tar -xf $(4).tmp --strip-components $(call FETCH_STRIP,$(3)) $(3) && \
  del $(4).tmp && (del $(4)-* 2>NUL || VER>NUL) && type nul > $(notdir $(5)) \
  $(foreach f,$(notdir $(3)),&& copy /b $(f) +$(COMMA)$(COMMA) >NUL)
else
# GNU tar can't read zip; tar detects compression when reading a file; archive preserves original mtimes, so
#  touch extracted files
FETCH_FILES_CMDS = \
  cd $(1) && curl -fL "$(2)" -o $(4).tmp && \
  $(if $(filter %.zip,$(2)),unzip -o -q -j $(4).tmp $(3),tar -xf $(4).tmp --strip-components $(call FETCH_STRIP,$(3)) $(3)) && \
  rm -f $(4).tmp $(4)-* && touch $(notdir $(5)) && touch $(notdir $(3))
endif

# extracted files get a recipe so make checks their mtimes again after the download and dependents are rebuilt
#  in the same run; stamp is created before files are touched, so files are newer than stamp; stamp is made
#  phony (always run) if any file is missing
define FETCH_FILES_RULES
$(if $(filter-out $(wildcard $(6)),$(6)),.PHONY: $(5))
$(5):
	$(call FETCH_FILES_CMDS,$(1),$(2),$(3),$(4),$(5))
$(6): $(5)
	@echo fetched $$@
GENERATED += $(5) $(6)
endef
