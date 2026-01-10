# Makefile fragment for MSVC x86 (32-bit) - v. 2018-11-28
# - for use with gnu make built for Windows: http://www.equation.com/servlet/equation.cmd?fa=make

# should we also define _UNICODE to use for C stdlib fns?
# common C and C++ flags
# /MD to use dynamic C runtime (msvcrt DLL); /MT to statically link C runtime (libcmt)
# if link complains about defaultlib ('LIBCMT' or 'MSVCRT'), try /verbose switch to which .lib is requesting
#  LIBCMT vs. MSVCRT
CFLAGS = /MT /Zc:preprocessor
# C++
CXX = cl.exe /nologo
CXXFLAGS = /std:c++14
#/GR- /D_HAS_EXCEPTIONS=0
# C
CC = cl.exe /nologo
CCFLAGS =
# linkerc
LD = link.exe /nologo
LDFLAGS = /SUBSYSTEM:WINDOWS
# resource compiler
RC = rc.exe
# WiX - tool for creating MSI installer
WIXCANDLE = "c:\Program Files (x86)\WiX Toolset v3.11\bin\candle.exe"
WIXLIGHT = "c:\Program Files (x86)\WiX Toolset v3.11\bin\light.exe"

DEBUG ?= 0
ifneq ($(DEBUG), 0)
  CFLAGS += /Od /Zi /DDEBUG
  LDFLAGS += /DEBUG
else
  # /GL + /LTCG for dist?
  CFLAGS += /O2 /Zi /DNDEBUG
  LDFLAGS += /DEBUG /INCREMENTAL:NO /OPT:REF /OPT:ICF
endif

# disable optimizations which make profiling difficult, esp. inlining
PROFILE ?= 0
ifneq ($(PROFILE), 0)
  CFLAGS += -fno-inline -g
endif

# assumes *FLAGS variables use deferred evaluation
CFLAGS += $(CFLAGS_PRIVATE)
CCFLAGS += $(CCFLAGS_PRIVATE)
CXXFLAGS += $(CXXFLAGS_PRIVATE)

# include paths
INCFLAGS = $(INC:%=/I%) $(INC_PRIVATE:%=/I%) $(INCSYS:%=/I%)

# defines
CFLAGS += $(DEFS:%=/D%) $(DEFS_PRIVATE:%=/D%)

# filter for cl with /showIncludes.  If the 5th character of line printed by cl happens to be ':'
#  it will get swallowed - we could add more matches, e.g., /C:"^[^N]" /C:"^N[^o] etc. if this is a problem.
DEPENDFILT = findstr /I /R /C:"^....[^:]" /C:"^Note:[ ]including[ ]file:[ ][ ]*$(DEPENDBASE)"

SRCBASE=$(basename $(SOURCES))
OBJ=$(SRCBASE:%=$(OBJDIR)/%.obj)
QUOTEOBJ=$(SRCBASE:%="$(OBJDIR)/%.obj")
DEPS=$(SRCBASE:%=$(OBJDIR)/%.d)
RESBASE=$(basename $(RESOURCES))
RES=$(RESBASE:%=$(OBJDIR)/%.res)
TGT=$(BUILDDIR)/$(TARGET).exe
# compiler will not create directories, so depend on existence of all directories in output folder
# sort removes duplicates (which cause make error)
BUILDDIRS=$(sort $(dir $(OBJ)))
# on Windows, existence check doesn't work for directories (?), so use an empty file in each directory instead
BUILDDIRSMADE=$(BUILDDIRS:%=%.made)
# packaging
ZIP = $(BUILDDIR)/$(ZIPFILE)
WIXBASE=$(basename $(WXS))
MSI = $(BUILDDIR)/$(WIXBASE).msi

.PHONY: all zip msi clean distclean rebuild_version_info

all: $(TGT)

zip: rebuild_version_info $(ZIP)

msi: rebuild_version_info $(MSI)

rebuild_version_info:
	powershell -Command "(Get-Item '$(SRC_WITH_VERSION_INFO)').LastWriteTime = Get-Date"

# force C/C++
$(OBJDIR)/$(FORCECPP): CFLAGS += /TP

# echo | set /p x="..." is trick to suppress newline; also used for each dependency added to file because I was
#  unable to get rid of trailing whitespace otherwise (code that didn't add trailing whitespace when run from
#  batch file did add one when pasted here!).  If the 5th character of line printed by cl happens to be ':'
#  it will get swallowed - we could add more matches, e.g., /C:"^[^N]" /C:"^N[^o] etc. if this is a problem.
$(OBJDIR)/%.obj: %.cpp
	@echo|set /p x="$@: " > $(basename $@).d
	($(CXX) /c $< /Fo:$@ /showIncludes $(CFLAGS) $(CXXFLAGS) $(INCFLAGS) || echo XXXDIE) | @FOR /F "tokens=1,2,3,*" %%A IN ('$(DEPENDFILT)') DO @IF "%%A"=="Note:" (echo|set /p x="%%D ">>$(basename $@).d) ELSE (@IF "%%A"=="XXXDIE" (exit 2) ELSE echo %%A %%B %%C %%D)

$(OBJDIR)/%.obj: %.cc
	@echo|set /p x="$@: " > $(basename $@).d
	($(CXX) /c $< /Fo:$@ /showIncludes $(CFLAGS) $(CXXFLAGS) $(INCFLAGS) || echo XXXDIE) | @FOR /F "tokens=1,2,3,*" %%A IN ('$(DEPENDFILT)') DO @IF "%%A"=="Note:" (echo|set /p x="%%D ">>$(basename $@).d) ELSE (@IF "%%A"=="XXXDIE" (exit 2) ELSE echo %%A %%B %%C %%D)

$(OBJDIR)/%.obj: %.c
	@echo|set /p x="$@: " > $(basename $@).d
	($(CC) /c $< /Fo:$@ /showIncludes $(CFLAGS) $(CCFLAGS) $(INCFLAGS) || echo XXXDIE) | @FOR /F "tokens=1,2,3,*" %%A IN ('$(DEPENDFILT)') DO @IF "%%A"=="Note:" (echo|set /p x="%%D ">>$(basename $@).d) ELSE (@IF "%%A"=="XXXDIE" (exit 2) ELSE echo %%A %%B %%C %%D)

$(OBJDIR)/%.res: %.rc
	$(RC) $(RCFLAGS) /fo $@ $<

$(TGT): $(OBJ) $(RES)
	$(LD) /out:$@ $^ $(LDFLAGS) $(LIBS)
	@echo Built $@

# unbelievable how difficult it is to get behavior of unix cp in Windows
$(ZIP): $(TGT) $(DISTRES)
	mkdir "$(BUILDDIR)/$(ZIPDIR)"
	powershell cp "$(TGT)" "$(BUILDDIR)/$(ZIPDIR)"
	cmd /C for %I in ( $(DISTRES) ) do powershell cp -r %I $(BUILDDIR)/$(ZIPDIR)
	powershell rm "$(BUILDDIR)/$(ZIPDIR)/shared/.git"
	(cd $(BUILDDIR) && tar.exe -acf $(ZIPFILE) $(ZIPDIR))
	rmdir /s /q "$(BUILDDIR)/$(ZIPDIR)"

$(MSI): $(TGT) $(DISTRES)
	mkdir $(BUILDDIR)\$(ZIPDIR)
	cp $(TGT) $(BUILDDIR)\$(ZIPDIR)
	cp -R $(DISTRES) $(BUILDDIR)\$(ZIPDIR)
	$(WIXCANDLE) -arch x64 -dProductVersion="$(APPVERSION)" -out $(BUILDDIR)\$(ZIPDIR)\$(WIXBASE).wixobj $(WXS)
	(cd $(BUILDDIR)\$(ZIPDIR) && $(WIXLIGHT) -ext WixUIExtension -cultures:en-us -out $(WIXBASE).msi $(WIXBASE).wixobj)
	mv $(BUILDDIR)\$(ZIPDIR)\$(WIXBASE).msi $(BUILDDIR)
	rmdir /s /q $(BUILDDIR)\$(ZIPDIR)

# | (pipe) operator causes make to just check for existence instead of timestamp
$(OBJ): | $(BUILDDIRSMADE)

# files that need to be generated, downloaded, etc.
$(OBJ): $(GENERATED)

# use quoted arg so that mkdir doesn't think '/' path separators are option switches
# || VER>NUL suppresses errors thrown if folders already exist
$(BUILDDIRSMADE):
	mkdir "$(dir $@)" || VER>NUL
	type nul > $@

clean:
	cd $(BUILDDIR) && del /S "*.obj" "*.d" "$(TARGET).exe"

distclean:
	rd /s /q ./Debug ./Release

# header dependency files
-include $(DEPS)
