##=============================================================================
## MIT License
##
## Copyright (c) 2019 Quentin Quadrat <lecrapouille@gmail.com>
##
## Permission is hereby granted, free of charge, to any person obtaining a copy
## of this software and associated documentation files (the "Software"), to deal
## in the Software without restriction, including without limitation the rights
## to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
## copies of the Software, and to permit persons to whom the Software is
## furnished to do so, subject to the following conditions:
##
## The above copyright notice and this permission notice shall be included in all
## copies or substantial portions of the Software.
##
## THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
## IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
## FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
## AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
## LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
## OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
## SOFTWARE.
##=============================================================================

###############################################################################
# Check if some variables have been defined before including this file.
###############################################################################

ifndef PROJECT_NAME
$(error PROJECT_NAME is mandatory for giving a name to your project.)
endif

ifndef PROJECT_VERSION
$(error PROJECT_VERSION is mandatory for giving a version to your binaries and libraries.)
endif

ifndef TARGET_NAME
$(error TARGET_NAME is mandatory for giving a name to your target executable or target library.)
endif

ifndef TARGET_DESCRIPTION
$(error TARGET_DESCRIPTION is mandatory for giving a short description of your target.)
endif

ifndef P
$(error P is mandatory for setting the local path to your project root folder.)
endif

ifndef M
$(error M is mandatory for setting the local path to MyMakefile root folder.)
endif

ifndef COMPILATION_MODE
$(error COMPILATION_MODE is mandatory for knowing if the project shall be compiled for debug or release or test or normal.)
endif

###############################################################################
# Cleanup project variables.
###############################################################################

# Unit tests
ifeq ($(COMPILATION_MODE),test)
    COMPILATION_MODE := debug
    USE_COVERAGE := 1
endif

# Force to convert to lower case the project and target name if requested.
ifdef FORCE_LOWER_CASE
    PROJECT_NAME := $(shell echo $(PROJECT_NAME) | tr A-Z a-z)
    TARGET_NAME := $(shell echo $(TARGET_NAME) | tr A-Z a-z)
    INTERNAL_LIBS := $(shell echo $(INTERNAL_LIBS) | tr A-Z a-z)
endif

# Do not accept libxxx as project name.
ifeq "$(filter lib%,$(PROJECT_NAME))" "$(PROJECT_NAME)"
    $(error PROJECT_NAME shall not start by lib.)
endif

# Do not accept libxxx as target name since its automagically managed.
ifeq "$(filter lib%,$(TARGET_NAME))" "$(TARGET_NAME)"
    $(error TARGET_NAME shall not start by lib. Library name is automatically managed by MyMakefile.)
endif

# Set the project version. Can be used for creating shared libraries.
PROJECT_VERSION_MAJOR := $(shell echo $(PROJECT_VERSION) | cut -d"." -f1)

###############################################################################
# Change the verbosity of the makefile. For example
# V=1 make -j8
###############################################################################

Q := @
ifdef V
    VERBOSE := $(V)
endif
ifdef VERBOSE
   Q :=
endif

###############################################################################
# Use bash instead of shell for Unix systems. Activate debug mode for Makefile.
###############################################################################

SHELL := /bin/bash
ifdef D
    DEBUG := $(D)
endif
ifdef DEBUG
    OLD_SHELL := $(SHELL)
    SHELL = $(warning [$@])$(OLD_SHELL) -x
endif

###############################################################################
# Detect the operating system: Unix, OSX and Windows and architecture.
###############################################################################

ifeq ($(OS),Windows_NT)
    OS := Windows
    WIN_ENV := $(patsubst CYGWIN%,Cygwin,$(OS))
    WIN_ENV := $(patsubst MSYS%,MSYS,$(WIN_ENV))
    WIN_ENV := $(patsubst MINGW%,MINGW,$(WIN_ENV))
    ifeq ($(PROCESSOR_ARCHITEW6432),AMD64)
        ARCH := 64
    endif
    ifeq ($(PROCESSOR_ARCHITECTURE),AMD64)
        ARCH := 64
    else
        ARCH := 32
    endif
# Compile the project for HTML5 with Emscripten or Exaequos
# https://github.com/exaequos ? It is simpler to consider as OS rather than
# target architecture.
else ifdef EXAEQUOS
    OS := Emscripten
    DEFINES += -DEXAEQUOS
else ifdef EMSCRIPTEN
    OS := Emscripten
# Linux or Darwin (MacOS)
else
    OS := $(shell sh -c 'uname -s 2>/dev/null || echo Unknown')
    ARCH := $(shell sh -c 'uname -m 2>/dev/null || echo Unknown')
    ifeq "$(OS)" "Unknown"
        $(error Unknown architecture. Abort!)
    endif
endif

###############################################################################
# Use macros for wrapping shell commands to make them architecture independent.
###############################################################################

ifeq "$(OS)" "Windows"
    MKDIR := mkdir
    RM := rmdir /S/Q
    CP := copy /y
    CD := dir
else
    MKDIR := mkdir -p
    RM := rm -fr
    CP := cp -fr
    CD := cd
    MV := mv
endif

###############################################################################
# Colorfull echo
###############################################################################

#? Echo command for colors
ECHO ?= echo
ECHO_COLOR ?= $(ECHO) -e

COLOR_DEFAULT := \033[00m
RED := \033[31m
GREEN := \033[32m
YELLOW := \033[33m
BLUE := \033[34m
PURPLE := \033[35m
CYAN := \033[36m
COLOR_INFO := \033[42m
COLOR_WARNING := \033[43m
COLOR_ERROR := \033[41m

###############################################################################
# Display predefined colorful messages
###############################################################################

print-simple = $(ECHO_COLOR) "$(PURPLE)*** $1: $(CYAN)$2$(COLOR_DEFAULT)$3"
print-from = $(ECHO_COLOR) "$(PURPLE)*** $1: $(CYAN)$2$(COLOR_DEFAULT) <= $(YELLOW)$3$(COLOR_DEFAULT)$4"
print-to = $(ECHO_COLOR) "$(PURPLE)*** $1: $(CYAN)$2$(COLOR_DEFAULT) => $(YELLOW)$3$(COLOR_DEFAULT)$4"
print-install = $(ECHO_COLOR) "$(PURPLE)*** Installing $1: $(CYAN)$2$(COLOR_DEFAULT) => $(YELLOW)$3$(COLOR_DEFAULT)$4"
print-info = $(ECHO_COLOR) "$(COLOR_INFO)!!! $1 !!!$(COLOR_DEFAULT)"
print-warning = $(ECHO_COLOR) "$(COLOR_WARNING)!!! $1 !!!$(COLOR_DEFAULT)"
print-error = $(ECHO_COLOR) "$(COLOR_ERROR)!!! $1 !!!$(COLOR_DEFAULT)"

###############################################################################
# Make does not offer a recursive recursive wildcard
# $1: list of folder to traverse
# $2: list of patterns
###############################################################################

rwildcard=$(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))
list-folders=$(sort $(dir $(call rwildcard,$1,*)))

###############################################################################
# Internal libs are supposed to be compiled previously and are by consequent
# present on the build folder.
###############################################################################
define internal-lib
    $(addsuffix .a,$(addprefix $(BUILD_DIR)/lib,$1))
endef

###############################################################################
# Set extensions for applications, static and shared libraries.
# Beware: for MacOS target a bundle application (xxx.app) is a folder not a
# binary which is inside the folder. See macro CREATE_MACOS_APP_BUNDLE.
# For ExaequOS we define .js but it also generates .wasm files.
###############################################################################

ifeq ($(OS),Linux)
    EXT_BINARY :=
    EXT_DYNAMIC_LIB := .so
    EXT_STATIC_LIB := .a
else ifeq ($(OS),Darwin)
    ifdef BUILD_MACOS_APP_BUNDLE
        EXT_BINARY := .app
    else
        EXT_BINARY :=
    endif
    EXT_DYNAMIC_LIB := .dylib
    EXT_STATIC_LIB := .a
else ifeq ($(OS),Windows)
    EXT_BINARY := .exe
    EXT_DYNAMIC_LIB := .dll
    EXT_STATIC_LIB := .lib
else ifeq ($(OS),Emscripten)
    ifdef EXAEQUOS
        EXT_BINARY := .js
    else
        EXT_BINARY := .html
    endif
    EXT_DYNAMIC_LIB := .so
    EXT_STATIC_LIB := .a
else
    $(error Unknown binary file extension for operating system '$(OS)')
endif

###############################################################################
## GNU standard paths.
## See https://www.gnu.org/prep/standards/html_node/Directory-Variables.html
###############################################################################

#? Define where the project will be installed: $(DESTDIR)$(PREFIX)/lib and $(DESTDIR)$(PREFIX)/bin.
DESTDIR ?=

#? Define where the project will be installed: $(DESTDIR)$(PREFIX)/lib and $(DESTDIR)$(PREFIX)/bin
PREFIX ?= /usr/local

#? Define where to install includes.
INCLUDEDIR ?= $(PREFIX)/include

#? Define where to install libraries.
LIBDIR ?= $(PREFIX)/lib

#? Define where to install pkgconfig files.
PKGLIBDIR ?= $(LIBDIR)/pkgconfig

#? Define where to install data and documentation.
DATADIR ?= $(PREFIX)/share

#? Define where to install standalone applications.
BINDIR ?= $(PREFIX)/bin

#? Define where to you can store temporary files.
TMPDIR ?= /tmp

# Final paths.
INSTALL_PKGLIBDIR := $(DESTDIR)$(PKGLIBDIR)
INSTALL_INCLUDEDIR := $(DESTDIR)$(INCLUDEDIR)/$(PROJECT_NAME)/$(PROJECT_VERSION)
INSTALL_LIBDIR := $(DESTDIR)$(LIBDIR)
INSTALL_BINDIR := $(DESTDIR)$(BINDIR)
INSTALL_DATADIR := $(DESTDIR)$(DATADIR)

# Override install folder for ExaequOS
ifdef EXAEQUOS
    INSTALL_BINDIR := $(abspath $(P)/media/localhost/$(TARGET_NAME)/exa)
endif

# Project installation paths on the operating system folder
INSTALL_PROJECT_DIR := $(INSTALL_DATADIR)/$(PROJECT_NAME)/$(PROJECT_VERSION)

# Project installation paths on the user folder
ifeq ($(OS),Linux)
    INSTALL_LOCAL_PROJECT_DATA_DIR += $(HOME)/.local/share/$(PROJECT_NAME)/$(PROJECT_VERSION)/data
else ifeq ($(OS),Windows)
    INSTALL_LOCAL_PROJECT_DATA_DIR += C:\\Users\\$(USER)\$(PROJECT_NAME)\\$(PROJECT_VERSION)\\data
else ifeq ($(OS),Darwin)
    ifdef BUILD_MACOS_APP_BUNDLE
        # Nothing to do it should be computed dynamically inside the bundle path
    endif
else ifeq ($(OS),Emscripten)
    INSTALL_LOCAL_PROJECT_DATA_DIR += data
else
    # Nothing to do
endif

###############################################################################
# Set internal project paths.
###############################################################################

#? Name of the directory holding the compilation artifacts.
BUILD ?= build
BUILD_DIR := $(abspath $(P)/$(BUILD))

# Root directory holding compiled C/C++ files.
OBJS_ROOT_DIR := $(BUILD_DIR)/$(COMPILATION_MODE)/objs-$(OS)/$(TARGET_NAME)

# Directory holding generated files.
GENERATION_DIR := $(BUILD_DIR)/generated
COMPILER_INFO_DONE := $(GENERATION_DIR)/.compiler-info

# Directory holding downloaded stuffs.
DOWNLOAD_DIR := $(BUILD_DIR)/downloaded

#? Name of the directory holding third-party (external) libraries.
THIRDPART ?= external
THIRDPART_DIR := $(abspath $(P)/$(THIRDPART))

#? Name of the directory holding documentation.
PROJECT_DOC ?= doc
PROJECT_DOC_DIR := $(abspath $(P)/$(PROJECT_DOC))

#? Name of the directory holding generated reports.
PROJECT_GENERATED_DOC ?= doc
PROJECT_GENERATED_DOC_DIR := $(abspath $(P)/$(PROJECT_GENERATED_DOC))

#? Directory where Doxygen is generated
GENERATED_DOXYGEN_DIR ?= $(PROJECT_GENERATED_DOC_DIR)/doxygen
DOXYGEN_INDEX_HTML := $(GENERATED_DOXYGEN_DIR)/html/index.html

#? Path of the generated profiling analysis
GPROF_ANALYSIS ?= $(PROJECT_GENERATED_DOC_DIR)/profiling/analysis.txt
GPROF_ANALYSIS_FOLDER := $(dir $(GPROF_ANALYSIS))

#? Path of the generated code coverage report
COVERAGE_RAPPORT ?= $(PROJECT_GENERATED_DOC_DIR)/code-coverage/coverage.html

#? Define the directory holding project data.
PROJECT_DATA ?= data
PROJECT_DATA_DIR := $(abspath $(P)/$(PROJECT_DATA))

#? Define the directory holding unit tests.
PROJECT_TESTS ?= tests

# Generated file holding project information used i.e. by MyLogger.
PROJECT_INFO := project_info
GENERATED_HEADER_PROJECT_INFO := $(GENERATION_DIR)/$(PROJECT_INFO).hpp

#? Path to the default icon for MacOS bundle applications.
ifeq ($(OS),Darwin)
    MACOS_BUNDLE_ICON ?= $(M)/assets/data/macos.icns
endif

PATH_PROJECT_LOGO ?= $(abspath $(M)/assets/icons/logo.png)
DOXYGEN_INPUTS ?= $(abspath $(P)/README.md $(P)/src $(P)/include)

# ? Path where to store temporary project files.
PROJECT_TEMP_DIR ?= $(TMPDIR)/$(PROJECT_NAME)/$(PROJECT_VERSION)

# Files that may not exist but giving information about the project.
PROJECT_INFO_FILES := AUTHORS LICENSE README.md ChangeLog VERSION

# File extensions matching for C++ header files.
REGEXP_CXX_HEADER_FILES ?= *.hpp *.ipp *.tpp *.hh *.h *.hxx *.incl

###############################################################################
# Rebuild the whole project if a Makefile has been modified
###############################################################################

LIST_MYMAKEFILES := $(M)/project/Makefile $(M)/rules/Makefile $(P)/Makefile
ifneq (,$(wildcard $(P)/Makefile.common))
    LIST_MYMAKEFILES += $(P)/Makefile.common
endif

###############################################################################
# MyMakefile paths
###############################################################################

PATH_MYMAKEFILE_DOXYFILE := $(abspath $(M)/assets/doxygen)
PATH_MYMAKEFILE_SCRIPTS := $(abspath $(M)/assets/scripts)
SCRIPT_CONFIG := $(PATH_MYMAKEFILE_SCRIPTS)/config.sh
SCRIPT_EXTRACT_STATIC_LIB := $(PATH_MYMAKEFILE_SCRIPTS)/alib.sh
SCRIPT_TAR_PROJECT := $(PATH_MYMAKEFILE_SCRIPTS)/targz.sh
SCRIPT_DOWNLOAD_THIRDPARTS := $(PATH_MYMAKEFILE_SCRIPTS)/download-external-libs.sh
SCRIPT_COMPILE_THIRDPARTS := $(PATH_MYMAKEFILE_SCRIPTS)/compile-external-libs.sh
USER_SCRIPT_DOWNLOAD_THIRDPARTS := $(THIRDPART_DIR)/download-external-libs.sh
USER_SCRIPT_COMPILE_THIRDPARTS := $(THIRDPART_DIR)/compile-external-libs.sh
SCRIPT_OBS := $(PATH_MYMAKEFILE_SCRIPTS)/obs.sh
