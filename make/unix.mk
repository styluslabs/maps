# (mostly) project-independent Makefile fragment for Unix - v. 2020-02-14
# - uses dependency files generated by gcc to automatically handle header dependencies
# - using a precompiled header with gcc (or clang) is not worth the hassle

# common C and C++ flags
CFLAGS += -MMD -Wall -Werror=return-type -Wno-strict-aliasing -Wno-class-memaccess
#-Wshadow
# C++; -Wconditionally-supported catches passing non-POD to varargs fn
CXX = g++
CXXFLAGS += --std=c++14 -Werror=conditionally-supported
#-fno-rtti -fno-exceptions -Wno-unused-parameter -Wno-unused-function -Wno-unused
# C
CC = gcc
CCFLAGS += --std=c99 -Werror=implicit-function-declaration -Werror=int-conversion
# linker
LD = g++
LDFLAGS +=

DEBUG ?= 0
ifneq ($(DEBUG), 0)
  CFLAGS += -O0 -g -DDEBUG
  # rdynamic needed to get backtrace symbols from, e.g., catchsegv
  LDFLAGS += -rdynamic
else
  CFLAGS += -O2 -DNDEBUG
endif

SANITIZE ?= 0
ifneq ($(SANITIZE), 0)
  CFLAGS += -fsanitize=address -fsanitize=undefined -fsanitize=float-divide-by-zero
  LDFLAGS += -lasan -lubsan
endif

# disable optimizations which make profiling difficult, esp. inlining; frame pointer needed for sampling
# -fno-inline ... let's try w/o this
PROFILE ?= 0
ifneq ($(PROFILE), 0)
  CFLAGS += -fno-omit-frame-pointer -g
  LDFLAGS += -rdynamic
endif

# gprof: -pg; utrace: -pg or -finstrument-functions
TRACE ?= 0
ifneq ($(TRACE), 0)
  CFLAGS += -finstrument-functions -g
  LDFLAGS += -finstrument-functions -rdynamic
endif

# project independent stuff
# pkg-config headers and libraries
ifneq ($(PKGS),)
  CFLAGS += $(shell pkg-config --cflags $(PKGS))
  LIBS += $(shell pkg-config --libs $(PKGS))
endif

# assumes *FLAGS variables use deferred evaluation
CFLAGS += $(CFLAGS_PRIVATE)
CCFLAGS += $(CCFLAGS_PRIVATE)
CXXFLAGS += $(CXXFLAGS_PRIVATE)

# include files
INCFLAGS = $(INC:%=-I%) $(INC_PRIVATE:%=-I%) $(INCSYS:%=-isystem %) $(INCFILES:%=-include %)

# defines
CFLAGS += $(DEFS:%=-D%) $(DEFS_PRIVATE:%=-D%)

SRCBASE=$(basename $(SOURCES))
OBJ=$(SRCBASE:%=$(OBJDIR)/%.o)
DEPS=$(SRCBASE:%=$(OBJDIR)/%.d)
TGT=$(BUILDDIR)/$(TARGET)
# gcc will not create directories, so depend on existence of all directories in output folder
# sort removes duplicates (which cause make error)
BUILDDIRS=$(sort $(dir $(OBJ)))

.PHONY: all tgz clean distclean sourcelist

all: $(TGT)

tgz: $(TGZ)

$(OBJDIR)/%.o: %.cpp
	$(CXX) -c $(CFLAGS) $(CXXFLAGS) $(INCFLAGS) -o $@ $<

$(OBJDIR)/%.o: %.cc
	$(CXX) -c $(CFLAGS) $(CXXFLAGS) $(INCFLAGS) -o $@ $<

$(OBJDIR)/%.o: %.c
	$(CC) -c $(CFLAGS) $(CCFLAGS) $(INCFLAGS) -o $@ $<

$(TGT): $(OBJ)
	$(LD) -o $@ $^ $(LDFLAGS) $(LIBS)

# strip $(TGT) -- remove symbols to get smaller exe
$(TGZ): $(TGT) $(DISTRES)
	strings $(TGT) | grep "^GLIBC_"
	mkdir -p $(BUILDDIR)/.dist
	mv $(TGT) $(BUILDDIR)/.dist
	rsync -Lvr --exclude .git $(DISTRES) $(BUILDDIR)/.dist
	mv $(BUILDDIR)/.dist $(BUILDDIR)/$(TARGET)
	(cd $(BUILDDIR) && tar --remove-files -czvf $@ $(TARGET))

# | (pipe) operator causes make to just check for existence instead of timestamp
$(OBJ): | $(BUILDDIRS)

$(BUILDDIRS):
	mkdir -p $(BUILDDIRS)

clean:
	rm -f $(TGT) $(OBJ) $(DEPS)

distclean:
	rm -rf ./Debug ./Release

sourcelist:
	@printf '%s\n' $(SOURCES)

# dependency files generated by gcc (-MMD switch) ("-include" ignores file if missing)
-include $(DEPS)