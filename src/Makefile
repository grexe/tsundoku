# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Gregor B. Rosenauer & Claude
## Haiku Generic Makefile v2.6 ##

## Fill in this file to specify the project being created, and the referenced
## Makefile-Engine will do all of the hard work for you. This handles any
## architecture of Haiku.

# MuPDF: from the mupdf1.28 packages (mupdf1.28_devel) by default, or from a source tree that has been built
# with "make build=release libs", see build.sh, if MUPDF_DIR is set.

# The name of the binary.
NAME = Tsundoku
TARGET_DIR= ../dist

# The type of binary, must be one of:
#	APP:	Application
#	SHARED:	Shared library or add-on
#	STATIC:	Static library archive
#	DRIVER: Kernel driver
TYPE = APP

# 	If you plan to use localization, specify the application's MIME signature.
APP_MIME_SIG = application/x-vnd.sen-labs.Tsundoku

#	The following lines tell Pe and Eddie where the SRCS, RDEFS, and RSRCS are
#	so that Pe and Eddie can fill them in for you.
#%{
# @src->@

#	Specify the source files to use. Full paths or paths relative to the
#	Makefile can be included. All files, regardless of directory, will have
#	their object files created in the common object directory. Note that this
#	means this Makefile will not work correctly if two source files with the
#	same name (source.c or source.cpp) are included from different directories.
#	Also note that spaces in folder names do not work well with this Makefile.
SRCS = annotations/AnnotationsView.cpp \
	ui/AttachmentsView.cpp \
	ui/BusyWindow.cpp \
	backend/CachedPage.cpp \
	backend/Document.cpp \
	backend/DjvuDocument.cpp \
	annotations/DocumentDraw.cpp \
	../lib/Bookmarks.cpp \
	annotations/DocumentStore.cpp \
	layout/DocumentLayout.cpp \
	annotations/DocumentWeb.cpp \
	app/EntryChangedMonitor.cpp \
	../lib/BbfInfo.cpp \
	backend/ComicArchive.cpp \
	../lib/ComicInfo.cpp \
	../lib/EpubCfi.cpp \
	../lib/EpubInfo.cpp \
	ui/FileInfoWindow.cpp \
	ui/FindTextWindow.cpp \
	app/Globals.cpp \
	app/Application.cpp \
	app/History.cpp \
	ui/LayoutUtils.cpp \
	annotations/NoteWindow.cpp \
	ui/OutlinesWindow.cpp \
	layout/PageLayout.cpp \
	scripting/Scripting.cpp \
	ui/PDFPrint.cpp \
	ui/PDFSearch.cpp \
	ui/PDFView.cpp \
	ui/PDFWindow.cpp \
	backend/PageRenderer.cpp \
	ui/PasswordWindow.cpp \
	ui/PreferencesWindow.cpp \
	ui/SidebarTabView.cpp \
	ui/PrintSettingsWindow.cpp \
	ui/PrintingProgressWindow.cpp \
	app/Settings.cpp \
	ui/StatusWindow.cpp \
	ui/TraceWindow.cpp \
	../lib/WebAnnotation.cpp \
	ui/EntryMenuItem.cpp \
	app/ResourceLoader.cpp \
	app/Thread.cpp

#	Specify the resource definition files to use. Full or relative paths can be
#	used.
RDEFS = app/Tsundoku.rdef

#	Specify the resource files to use. Full or relative paths can be used.
#	Both RDEFS and RSRCS can be utilized in the same Makefile.
RSRCS =

# End Pe/Eddie support.
# @<-src@
#%}

#	Specify libraries to link against.
#	There are two acceptable forms of library specifications:
#	-	if your library follows the naming pattern of libXXX.so or libXXX.a,
#		you can simply specify XXX for the library. (e.g. the entry for
#		"libtracker.so" would be "tracker")
#
#	-	for GCC-independent linking of standard C++ libraries, you can use
#		$(STDCPPLIBS) instead of the raw "stdc++[.r4] [supc++]" library names.
#
#	- 	if your library does not follow the standard library naming scheme,
#		you need to specify the path to the library and it's name.
#		(e.g. for mylib.a, specify "mylib.a" or "path/mylib.a")
LIBS = shared be textencoding columnlistview tracker translation localestub xml2 zip archive djvulibre $(STDCPPLIBS)
ifdef MUPDF_DIR
LIBS += $(MUPDF_DIR)/build/release/libmupdf.a $(MUPDF_DIR)/build/release/libmupdf-third.a \
	freetype harfbuzz openjp2 jbig2dec brotlidec brotlienc brotlicommon jpeg z
else
LIBS += mupdf1.28
endif

#	Specify additional paths to directories following the standard libXXX.so
#	or libXXX.a naming scheme. You can specify full paths or paths relative
#	to the Makefile. The paths included are not parsed recursively, so
#	include all of the paths where libraries must be found. Directories where
#	source files were specified are	automatically included.
LIBPATHS = /boot/home/config/develop/lib

#	Additional paths to look for system headers. These use the form
#	"#include <header>". Directories that contain the files in SRCS are
#	NOT auto-included here.
SYSTEM_INCLUDE_PATHS = /system/develop/headers/private/interface /system/develop/headers/libxml2
ifdef MUPDF_DIR
SYSTEM_INCLUDE_PATHS += $(MUPDF_DIR)/include
else
SYSTEM_INCLUDE_PATHS += /system/develop/headers/mupdf1.28 /boot/home/config/develop/headers/mupdf1.28
endif

#	Additional paths paths to look for local headers. These use the form
#	#include "header". Directories that contain the files in SRCS are
#	automatically included.
LOCAL_INCLUDE_PATHS =

#	Specify the level of optimization that you want. Specify either NONE (O0),
#	SOME (O1), FULL (O2), or leave blank (for the default optimization level).
OPTIMIZE := SOME

# 	Specify the codes for languages you are going to support in this
# 	application. The default "en" one must be provided too. "make catkeys"
# 	will recreate only the "locales/en.catkeys" file. Use it as a template
# 	for creating catkeys for other languages. All localization files must be
# 	placed in the "locales" subdirectory.
LOCALES = ca cs de el en_AU en_GB en es fi fr fur hi id ja pl pt pt_BR ro ru sv th tr uk

#	Specify all the preprocessor symbols to be defined. The symbols will not
#	have their values set automatically; you must supply the value (if any) to
#	use. For example, setting DEFINES to "DEBUG=1" will cause the compiler
#	option "-DDEBUG=1" to be used. Setting DEFINES to "DEBUG" would pass
#	"-DDEBUG" on the compiler's command line.
DEFINES =

#	Specify the warning level. Either NONE (suppress all warnings),
#	ALL (enable all warnings), or leave blank (enable default warnings).
WARNINGS = ALL

#	With image symbols, stack crawls in the debugger are meaningful.
#	If set to "TRUE", symbols will be created.
SYMBOLS :=

#	Includes debug information, which allows the binary to be debugged easily.
#	If set to "TRUE", debug info will be created.
DEBUGGER :=

#	Specify any additional compiler flags to be used.
COMPILER_FLAGS = -Wno-write-strings

#	Specify any additional linker flags to be used.
LINKER_FLAGS = $(shell pkg-config --libs freetype2)

#	Specify the version of this binary. Example:
#		-app 3 4 0 d 0 -short 340 -long "340 "`echo -n -e '\302\251'`"1999 GNU GPL"
#	This may also be specified in a resource.
APP_VERSION :=

#	(Only used when "TYPE" is "DRIVER"). Specify the desired driver install
#	location in the /dev hierarchy. Example:
#		DRIVER_PATH = video/usb
#	will instruct the "driverinstall" rule to place a symlink to your driver's
#	binary in ~/add-ons/kernel/drivers/dev/video/usb, so that your driver will
#	appear at /dev/video/usb when loaded. The default is "misc".
DRIVER_PATH =

## Include the Makefile-Engine
DEVEL_DIRECTORY := \
	$(shell findpaths -r "makefile_engine" B_FIND_PATH_DEVELOP_DIRECTORY)
include $(DEVEL_DIRECTORY)/etc/makefile-engine
