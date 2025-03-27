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
# C/C++ compilers
###############################################################################

#? Select the C++ standard.
CXX_STANDARD ?= --std=c++14

ifeq ($(OS),Emscripten)
    #? Define your C compilator.
    EMCC ?= emcc
    CC := $(EMCC)

    #? Define your C++ compilator.
    EMCXX ?= em++
    CXX := $(EMCXX)

    #? Define your static library tool and its default flags.
    EMAR ?= emar
    AR := $(EMAR)

    # Force default flags for ar command.
    ifdef EXAEQUOS
        ARFLAGS := crs
    endif

    #? command for running the application
    RUN := emrun

    # Do not strip symbols
    STRIP :=
else
    #? Define your C compilator.
    CC ?=

    #? Define your C++ compilator.
    CXX ?=

    #? Define your static library tool and its default flags.
    AR ?= ar

    #? Define default flags for ar command.
    ARFLAGS ?= crs

    #? command for running the application
    RUN ?=

    #? Strip command. See http://reverse.lostrealm.com/protect/strip.html
    ifeq ($(OS),Linux)
        ifeq ($(COMPILATION_MODE),release)
            STRIP ?= strip -R .comment -R .note -R .note.ABI-tag
        else
            STRIP :=
        endif
    endif
endif

###############################################################################
# Search order for finding project:
#  - data folder inside the project (when compiled in debug mode).
#  - data folder inside the user operating system (read-write access).
#  - data folder installed on the operating system (read-only access).
#  - folder data on the same folder than the exectuable (maybe read-only access).
#  - folder local to the executable (maybe read-only access).
###############################################################################

SEARCH_DATA_PATHS := $(INSTALL_LOCAL_PROJECT_DATA_DIR):$(INSTALL_PROJECT_DIR)/$(PROJECT_DATA):$(PROJECT_DATA):.
ifeq ($(COMPILATION_MODE),debug)
    SEARCH_DATA_PATHS := $(PROJECT_DATA_DIR):$(SEARCH_DATA_PATHS)
endif

###############################################################################
# Compilation flags. Mimic a -Wwhole-flags if it would have existed.
###############################################################################

ifneq ($(filter %.cpp,$(strip $(SRC_FILES) $(LIB_FILES))),)
    COMPILER := $(CXX) $(CXX_STANDARD)
else
    COMPILER := $(CC)
endif

COMPILER_NAME := $(shell $(COMPILER) --version | head -1 | cut -d" " -f1 | cut -d"-" -f1)
COMPILER_VERSION := $(shell $(COMPILER) --version | head -1 | grep -o -E '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
DEFINES += -DCOMPILER_NAME=\"$(COMPILER_NAME)\"
DEFINES += -DCOMPILER_VERSION=\"$(COMPILER_VERSION)\"

# C++ flags
ifdef CXXFLAGS
    $(warning You have override default compilation flags CXXFLAGS by your own flags $(CXXFLAGS) better to set $(USER_CXXFLAGS))
endif

# Avoid temporary files, speeding up builds
CXXFLAGS += -pipe

# Clang and GCC common flags
CXXFLAGS += -Wall -Wextra -Wuninitialized -Wundef -Wunused \
-Wunused-result -Wunused-parameter -Wtype-limits \
-Wcast-align -Wcast-qual -Wconversion -Wfloat-equal \
-Wpointer-arith -Wswitch-enum -Wpedantic -Wpacked \
-Wdeprecated -Wvariadic-macros -Wvla -Wsign-conversion -Wshadow -Wold-style-cast

# Clang specific
ifeq ($(COMPILER_NAME),clang)
    CXXFLAGS += -Wconditional-uninitialized \
    -Wused-but-marked-unused -Wunused-member-function \
    -Wunused-getter-return-value -Wunused-exception-parameter \
    -Wunneeded-member-function -Wunneeded-internal-declaration -Wshadow-all \
    -Wunreachable-code-aggressive -Wvector-conversion -Wzero-length-array \
    -Wthread-safety -Werror=implicit-function-declaration \
    -Wtautological-compare -Wno-unused-command-line-argument -Wundefined-reinterpret-cast -Wsuper-class-method-mismatch
    # GCC specific
else ifeq ($(COMPILER_NAME),$(filter $(COMPILER_NAME),g++ gcc))
    GCC_GREATER_V4_8 := $(shell expr $(COMPILER_VERSION) \>= 40800)
    GCC_GREATER_V4_9 := $(shell expr $(COMPILER_VERSION) \>= 40900)
    GCC_GREATER_V6 := $(shell expr $(COMPILER_VERSION) \>= 60000)
    GCC_GREATER_V9 := $(shell expr $(COMPILER_VERSION) \>= 90000)
    ifeq ($(GCC_GREATER_V6),1)
        CXXFLAGS += -Wtautological-compare
    else ifeq ($(GCC_GREATER_V9),1)
        CXXFLAGS += -Wpessimizzing-move
    endif
endif

# Flags for hardening the binary
CXXFLAGS += -Wformat -Wformat-security -Werror=format-security -D_FORTIFY_SOURCE=2 -fPIE -fexceptions
ifeq ($(COMPILER_NAME),$(filter $(COMPILER_NAME),g++ gcc))
    ifeq ($(GCC_GREATER_V4_9),1)
        CXXFLAGS += -fstack-protector-strong
    endif
endif

# C flags
ifdef CCFLAGS
    $(warning You have override default compilation flags CCFLAGS by your own flags $(CCFLAGS) better to set $(USER_CCFLAGS))
endif

# Avoid temporary files, speeding up builds
CCFLAGS += -pipe

# Warms about C
CCFLAGS += -Wstrict-prototypes

# Clang and GCC common flags
CCFLAGS += -Wall -Wextra -Wuninitialized -Wundef -Wunused \
-Wunused-result -Wunused-parameter -Wtype-limits \
-Wcast-align -Wcast-qual -Wconversion -Wfloat-equal \
-Wpointer-arith -Wswitch-enum -Wpedantic -Wpacked \
-Wdeprecated -Wvariadic-macros -Wvla -Wsign-conversion -Wshadow

# Clang specific
ifeq ($(COMPILER_NAME),clang)
    CCFLAGS += -Wconditional-uninitialized \
    -Wused-but-marked-unused -Wunused-member-function \
    -Wunused-getter-return-value -Wunused-exception-parameter \
    -Wunneeded-member-function -Wunneeded-internal-declaration -Wshadow-all \
    -Wunreachable-code-aggressive -Wvector-conversion -Wzero-length-array \
    -Wthread-safety -Werror=implicit-function-declaration \
    -Wtautological-compare -Wno-unused-command-line-argument
# GCC specific
else ifeq ($(COMPILER_NAME),$(filter $(COMPILER_NAME),g++ gcc))
    GCC_GREATER_V4_8 := $(shell expr $(COMPILER_VERSION) \>= 40800)
    GCC_GREATER_V4_9 := $(shell expr $(COMPILER_VERSION) \>= 40900)
    GCC_GREATER_V6 := $(shell expr $(COMPILER_VERSION) \>= 60000)
    GCC_GREATER_V9 := $(shell expr $(COMPILER_VERSION) \>= 90000)
    ifeq ($(GCC_GREATER_V6),1)
        CCFLAGS += -Wtautological-compare
    else ifeq ($(GCC_GREATER_V9),1)
        CCFLAGS += -Wpessimizzing-move
    endif
endif

# Flags for hardening the binary
CCFLAGS += -Wformat -Wformat-security -Werror=format-security -D_FORTIFY_SOURCE=2 -fPIE -fexceptions
ifeq ($(COMPILER_NAME),$(filter $(COMPILER_NAME),g++ gcc))
    ifeq ($(GCC_GREATER_V4_9),1)
        CCFLAGS += -fstack-protector-strong
    endif
endif

###############################################################################
# Compilation flags for compiling shared libraries.
###############################################################################

LDFLAGS += -pie
ifeq ($(OS),Darwin)
    DLL_CXXFLAGS := -fPIC
    DLL_LDFLAGS := -dynamiclib
    DEFINES += -DAPI_LINKAGE=
else ifeq ($(OS),Linux)
    DLL_CXXFLAGS := -fPIC
    DLL_LDFLAGS := -shared
    DEFINES += -DAPI_LINKAGE=
else ifeq ($(OS),Windows)
    CXXFLAGS := -fPIC
    DLL_LDFLAGS := -shared
	ifdef DLL_LINKAGE_EXPORT
        DEFINES += -DAPI_LINKAGE="__declspec(dllexport)"
	else
        DEFINES += -DAPI_LINKAGE="__declspec(dllimport)"
	endif
    ifeq ($(WIN_ENV),MINGW)
        LDFLAGS += -lmingw32
    else
        $(error Unknown shared library file extension for this architecture)
    endif
endif

CXXFLAGS += $(DLL_CXXFLAGS)
CCFLAGS += $(DLL_CXXFLAGS)

# Creating shared libraries from static ones.
# Note: since 2024 -noall_load are removed on MacOS.
ifneq ($(OS),Darwin)
    LINKER_ALL_LOAD=-Wl,--whole-archive
    LINKER_NO_ALL_LOAD=-Wl,--no-whole-archive
endif

###############################################################################
# Profile code.
###############################################################################

ifdef USE_GPROF
    USE_BACKWARD :=
    CXXFLAGS += -pg
    LDFLAGS += -pg
    ifdef STRIP
        $(warning Release mode with stripped symbols was desired but gprof need them strip command disabled!)
        STRIP :=
    endif
endif

###############################################################################
# Use AddressSanitizer.
###############################################################################

ifdef USE_ASAN
    USE_BACKWARD :=
    CXXFLAGS += -fsanitize=address -fno-omit-frame-pointer
    LDFLAGS += -fsanitize=address -static-libasan
    SANITIZER := ASAN_OPTIONS=symbolize=1 ASAN_SYMBOLIZER_PATH=$(shell which llvm-symbolizer)
endif

###############################################################################
# TODO: -O1 is needed for hardening
# TODO: -O0 seems better for debuging with g++
# GCC and clang Optimization flags
###############################################################################

ifeq ($(COMPILATION_MODE),debug)
    ifeq ($(OS),Linux)
        USE_BACKWARD := 1
        USE_DEBUG_MACRO := 1
    else
        USE_BACKWARD :=
        USE_DEBUG_MACRO :=
    endif
    OPTIM_FLAGS := -O0 -g3 -fasynchronous-unwind-tables
    DEFINES += -UNDEBUG -D_GLIBCXX_ASSERTIONS
else ifeq ($(COMPILATION_MODE),release)
    USE_BACKWARD :=
    USE_DEBUG_MACRO :=
    OPTIM_FLAGS := -O2
    DEFINES += -DNDEBUG
else ifeq ($(COMPILATION_MODE),normal)
    USE_BACKWARD :=
    USE_DEBUG_MACRO :=
    OPTIM_FLAGS := -O2 -g
    DEFINES += -UNDEBUG -D_GLIBCXX_ASSERTIONS
else
    $(error COMPILATION_MODE shall be debug or release but get $(COMPILATION_MODE))
endif

###############################################################################
# Use code coverage. Beware this will replace optimization flags !
###############################################################################

ifdef USE_COVERAGE
    OPTIM_FLAGS := -O1
    ifeq ($(COMPILATION_MODE),debug)
        OPTIM_FLAGS += -g
    endif
    CXXFLAGS += --coverage
    LDFLAGS += --coverage
endif

###############################################################################
# Activate backward-cpp library if desired.
###############################################################################

ifdef USE_BACKWARD
    ifeq ($(COMPILATION_MODE),release)
        $(warning Release mode was desired as well using backward-cpp will force debug mode)
        USE_BACKWARD :=
    endif
    ifeq ($(OS),Windows)
        $(warning Cannot use backward-cpp lib for Windows. USE_BACKWARD will be disabled)
        USE_BACKWARD :=
    else ifeq ($(OS),Darwin)
        $(warning Cannot use backward-cpp lib for MacOS X. USE_BACKWARD will be disabled)
        USE_BACKWARD :=
    else ifeq ($(OS),Emscripten)
        $(warning Cannot use backward-cpp lib for Emscripten. USE_BACKWARD will be disabled)
        USE_BACKWARD :=
    else ifdef USE_GPROF
        USE_BACKWARD :=
    else
    endif
endif

ifdef USE_BACKWARD
    PRIVATE_SRC_FILES += $(DOWNLOAD_DIR)/backward-cpp/backward.cpp
    PRIVATE_SLIB_FILES += $(DOWNLOAD_DIR)/backward-cpp/backward.cpp
    INCLUDES += -I$(DOWNLOAD_DIR)/backward-cpp
    VPATH += $(DOWNLOAD_DIR)/backward-cpp
    DEFINES += -DBACKWARD_HAS_DW=1 -DBACKWARD_HAS_BFD=0 -DBACKWARD_HAS_DWARF=0
    DEFINES += -DBACKWARD_HAS_UNWIND=1 -DBACKWARD_HAS_BACKTRACE=0
    DEFINES += -DBACKWARD_HAS_BACKTRACE_SYMBOL=1 -DBACKWARD_HAS_LIBUNWIND=0
    LINKER_FLAGS += -ldw
endif

###############################################################################
# Activate debug if dbg-macro library is desired
###############################################################################

ifdef USE_DEBUG_MACRO
    ifeq ($(COMPILATION_MODE),release)
        $(warning Release mode desired but USE_DEBUG_MACRO will force debug mode)
        USE_DEBUG_MACRO :=
    endif
endif

ifdef USE_DEBUG_MACRO
    INCLUDES += -I$(OBJS_ROOT_DIR)/dbg-macro
    DEFINES += -DBG_MACRO_NO_WARNING
endif

###############################################################################
# Manage the C/C++ project.
###############################################################################

# static libs are just zipped object files. When you want to merge static libs
# you have to extract first their object files then combined them into a new
# static lib.
# BEWARE: do not include $(INTERNAL_LIBS) since we do not want internal libs
# include each others: let them independent and let the generated pkg_config
# include them. Let only include static thirdpart libs inside internal libs
# since we do not want to force installing them on the user system.
define extract-objs-from-static-libs
    @$(SCRIPT_EXTRACT_STATIC_LIB) $(OBJS_ROOT_DIR) $(THIRDPART_LIBS)
    $(eval EXTRACTED_OBJS=$(addsuffix /*.o,$(addprefix $(OBJS_ROOT_DIR)/,$(basename $(notdir $(filter-out \,$(THIRDPART_LIBS) $(INTERNAL_LIBS)))))))
endef

# Else, the user have supposed given some C/C++ file to compile. Check if at least one is present.
ifeq ($(strip $(LIB_FILES) $(SRC_FILES)),)
    $(error Found no C/C++ object files to compile. Please set correctly LIB_DIRS or/and SRC_DIRS)
endif

# Subfolders to create inside build folder
BUILD_FOLDERS :=

# Convert C/C++ application files to object files
ifdef SRC_FILES
    SRC_FILES := $(foreach d,$(SRC_FILES) $(PRIVATE_SRC_FILES),$(abspath $d))
    SRC_FILES := $(patsubst $(abspath $(P))/%,%,$(SRC_FILES))
    BUILD_FOLDERS += $(dir $(SRC_FILES))
    OBJS := $(addprefix $(OBJS_ROOT_DIR)/,$(addsuffix .o,$(basename $(SRC_FILES))))
endif

# Convert C/C++ library files to object files
ifdef LIB_FILES
    LIB_FILES := $(foreach d,$(LIB_FILES) $(PRIVATE_LIB_FILES),$(abspath $d))
    LIB_FILES := $(patsubst $(abspath $(P))/%,%,$(LIB_FILES))
    BUILD_FOLDERS += $(dir $(LIB_FILES))
    LIB_OBJS := $(addprefix $(OBJS_ROOT_DIR)/,$(addsuffix .o,$(basename $(LIB_FILES))))
endif

BUILD_FOLDERS := $(sort $(BUILD_FOLDERS))
VPATH += $(abspath $(P))

# Do we compile a library or a standalone application ?
ifdef LIB_OBJS
    # libfoo.a and libfoo.so and avoid adding extra lib if already set
    SHARED_LIB_NAME := $(TARGET_NAME)$(EXT_DYNAMIC_LIB)
    STATIC_LIB_NAME := $(TARGET_NAME)$(EXT_STATIC_LIB)
    ifneq "$(filter lib%,$(TARGET_NAME))" "$(TARGET_NAME)"
        SHARED_LIB_NAME := lib$(SHARED_LIB_NAME)
        STATIC_LIB_NAME := lib$(STATIC_LIB_NAME)
    endif

    # build/libfoo.a and build/libfoo.so
    # For Emscripten and ExaequOS shared lib are not managed
    TARGET_STATIC_LIB_NAME := $(BUILD_DIR)/$(STATIC_LIB_NAME)
    ifneq ($(OS),Emscripten)
        TARGET_SHARED_LIB_NAME := $(BUILD_DIR)/$(SHARED_LIB_NAME)
        PKG_FILE_NAME := $(TARGET_NAME).pc
        TARGET_PKG_FILE_NAME := $(BUILD_DIR)/$(PKG_FILE_NAME)
    else
        SHARED_LIB_NAME :=
    endif
endif
ifdef OBJS
    TARGET_BINARY_BASE_NAME := $(BUILD_DIR)/$(TARGET_NAME)
    TARGET_BINARY_NAME := $(TARGET_BINARY_BASE_NAME)$(EXT_BINARY)
endif

# Convert <path>/libtoto.a into -ltoto
SHORT_INTERNAL_LIBS := $(subst $(BUILD_DIR)/lib,-l,$(INTERNAL_LIBS))
SHORT_INTERNAL_LIBS := $(subst .a,,$(SHORT_INTERNAL_LIBS))

INCLUDES := $(addprefix -I,$(abspath $(INCLUDES) $(GENERATION_DIR) $(DOWNLOAD_DIR)))

###############################################################################
# Store files dependencies in *.d files: when a file is modified this will
# compile others which depend on it.
###############################################################################

DEPFLAGS = -MT $(abspath $@) -MMD -MP -MF $(abspath $(OBJS_ROOT_DIR)/$*.Td)
POSTCOMPILE = mv -f $(abspath $(OBJS_ROOT_DIR)/$*.Td $(OBJS_ROOT_DIR)/$*.d)

###############################################################################
# Libraries installed on the operating system and known from pkg-config.
###############################################################################

ifdef PKG_LIBS
    # Define where to install pkg-config file
    ifdef EXAEQUOS
        PKG_CONFIG_SEARCH_PATH := $(abspath $(P)/exapkgs/pkgconfigs)
    else
        PKG_CONFIG_SEARCH_PATH ?=
    endif

    ifdef PKG_CONFIG_SEARCH_PATH
        SEARCH_CMD := PKG_CONFIG_PATH="$(PKG_CONFIG_SEARCH_PATH)"
    endif

    PKGCFG_LIBS := `$(SEARCH_CMD) pkg-config --libs $(PKG_LIBS)`
    PKGCFG_CFLAGS := `$(SEARCH_CMD) pkg-config --cflags $(PKG_LIBS)`
    SEARCH_CMD :=
endif

###############################################################################
# Progress bar
###############################################################################

PROGRESSBAR_PERCENT := 0
PROGRESSBAR_DISPLAY := 0
PROGRESSBAR_COUNT_ELEMENTS := $(words $(OBJS) $(LIB_OBJS))
$(eval PROGRESSBAR_INCREMENT=$(shell echo "100 / $(PROGRESSBAR_COUNT_ELEMENTS)" | bc -l))
define progressbar-update-percent
    $(eval PROGRESSBAR_PERCENT=$(shell echo "$(PROGRESSBAR_PERCENT) + $(PROGRESSBAR_INCREMENT)" | bc -l))
    $(eval PROGRESSBAR_DISPLAY=$(shell echo "($(PROGRESSBAR_PERCENT) + 0.5) / 1" | bc))
endef

progress-in = $(call progressbar-update-percent) $(ECHO_COLOR) "$(PURPLE)[$(PROGRESSBAR_DISPLAY)%] $1 $(CYAN)$2$(COLOR_DEFAULT)$(COLOR_DEFAULT) <= $(YELLOW)$3$(COLOR_DEFAULT)"
progress-out = $(call progressbar-update-percent) $(ECHO_COLOR) "$(PURPLE)[$(PROGRESSBAR_DISPLAY)%] $1 $(CYAN)$2$(COLOR_DEFAULT)$(COLOR_DEFAULT) => $(YELLOW)$3$(COLOR_DEFAULT)"

###############################################################################
# Installing a list of files $1 to destination $2.
###############################################################################

define HELPER_INSTALL_LIST_OF_FILES
	@$(call print-install,$(TARGET_NAME),$1,$2)
	$(Q)$(foreach folder,$(sort $(dir $1)),install -d -m 755 "$(abspath $2/$(folder))";)
 	$(Q)$(foreach file,$1,install -m 644 "$(abspath $(file))" "$2/$(file)";)
endef

###############################################################################
# From $1, install a list of files $2 to destination $3.
###############################################################################

define HELPER_INSTALL_LIST_OF_FILES_FROM_FOLDER
	@$(call print-install,$(TARGET_NAME),$2,$3, [$(abspath $1)])
	$(Q)$(foreach folder,$(sort $(dir $2)),install -d -m 755 "$(abspath $3/$(folder))";)
	$(Q)$(foreach file,$2,install -m 644 "$(abspath $1/$(file))" "$3/$(file)";)
endef

###############################################################################
# Installing a list of folders $2 to destination $3. $1 is for the title.
###############################################################################

# Does not work since * can be either folder or files and we do not have function
# to distinguish them
#@$(call HELPER_INSTALL_LIST_OF_FILES,$1,$(call rwildcard,$2,*),$3)

define HELPER_INSTALL_LIST_OF_FOLDERS
	@$(call print-install,$(TARGET_NAME),$1,$2)
    $(Q)install -d -m 755 $(foreach d,$1,$2/$(d))
    $(Q)$(foreach d,$1,cp -fr $(d)/. $2/$(d); )
endef

define HELPER_INSTALL_OPTIONAL_FOLDERS
	@$(if $(wildcard $1),$(call HELPER_INSTALL_LIST_OF_FOLDERS,$(wildcard $1),$2))
endef

###############################################################################
# Installing a list of C++ header files from $1 to destination $2.
###############################################################################

define HELPER_INSTALL_CXX_HEADER_FILES
	@$(call HELPER_INSTALL_LIST_OF_FILES_FROM_FOLDER,$1,$(strip $(subst $1/,,$(call rwildcard,$1,$(REGEXP_CXX_HEADER_FILES)))),$2)
endef

###############################################################################
# Installing files that may not exist from folder $1 to destination folder $2.
###############################################################################

define HELPER_INSTALL_OPTIONAL_FILES
	@$(call HELPER_INSTALL_LIST_OF_FILES,$(wildcard $1),$2)
endef

###############################################################################
# Install the project doc and generated doc folders.
###############################################################################

define INSTALL_PROJECT_DOC
	@$(call HELPER_INSTALL_OPTIONAL_FOLDERS,$(PROJECT_DOC) $(PROJECT_GENERATED_DOC),$(INSTALL_PROJECT_DIR))
endef

###############################################################################
# Install the project data folder and its content.
###############################################################################

define INSTALL_PROJECT_DATA
	@$(call HELPER_INSTALL_OPTIONAL_FOLDERS,$(PROJECT_DATA),$(INSTALL_PROJECT_DIR))
endef

###############################################################################
# Install the project information files: license, readme, version, changelog ...
###############################################################################

define INSTALL_PROJECT_INFO_FILES
	@$(call HELPER_INSTALL_OPTIONAL_FILES,$(PROJECT_INFO_FILES),$(INSTALL_PROJECT_DIR))
endef

###############################################################################
# Install the project binary  (the project target shall be a stand alone app).
###############################################################################

ifndef EXAEQUOS
  define INSTALL_PROJECT_BINARY
	@$(call print-install,$(TARGET_NAME),$(TARGET_NAME),$(INSTALL_BINDIR))
	$(Q)install -d -m 755 $(INSTALL_BINDIR)
	$(Q)install -m 644 $(TARGET_BINARY_NAME) $(INSTALL_BINDIR)/$(TARGET_NAME)-$(PROJECT_VERSION)
	$(Q)ln -sf $(INSTALL_BINDIR)/$(TARGET_NAME)-$(PROJECT_VERSION) $(INSTALL_BINDIR)/$(TARGET_NAME)
  endef
else
  define INSTALL_PROJECT_BINARY
	@$(call print-install,$(TARGET_NAME),$(TARGET_NAME),$(INSTALL_BINDIR))
	$(Q)install -d -m 755 $(INSTALL_BINDIR)
	$(Q)install -m 644 $(TARGET_BINARY_BASE_NAME).js $(INSTALL_BINDIR)/$(TARGET_NAME).js
	$(Q)install -m 644 $(TARGET_BINARY_BASE_NAME).wasm $(INSTALL_BINDIR)/$(TARGET_NAME).wasm
	$(call CREATE_EXAEQUOS_HTML,exa.html)
  endef
endif

###############################################################################
# Install the pkg-config file (the project target shall be a library).
###############################################################################

define INSTALL_PROJECT_PKG_CONFIG
	@$(call print-install,$(TARGET_NAME),$(PKG_FILE_NAME),$(INSTALL_PKGLIBDIR))
	$(Q)install -d -m 755 $(INSTALL_PKGLIBDIR)
	$(Q)install -m 644 $(TARGET_PKG_FILE_NAME) $(INSTALL_PKGLIBDIR)/$(TARGET_NAME)-$(PROJECT_VERSION).pc
	$(Q)ln -snf $(INSTALL_PKGLIBDIR)/$(TARGET_NAME)-$(PROJECT_VERSION).pc $(INSTALL_PKGLIBDIR)/$(PKG_FILE_NAME)
endef

###############################################################################
# Install shared libraries (the project target shall be a library).
###############################################################################

define INSTALL_PROJECT_SHARED_LIBRARIES
	@$(call print-install,$(TARGET_NAME),$(SHARED_LIB_NAME),$(INSTALL_LIBDIR))
	$(Q)install -d -m 755 $(INSTALL_LIBDIR)
	$(Q)install -m 644 $(TARGET_SHARED_LIB_NAME) $(INSTALL_LIBDIR)/$(SHARED_LIB_NAME).$(PROJECT_VERSION)
	$(Q)ln -snf $(INSTALL_LIBDIR)/$(SHARED_LIB_NAME).$(PROJECT_VERSION) $(INSTALL_LIBDIR)/$(SHARED_LIB_NAME)
	$(Q)ln -snf $(INSTALL_LIBDIR)/$(SHARED_LIB_NAME).$(PROJECT_VERSION) $(INSTALL_LIBDIR)/$(SHARED_LIB_NAME).$(PROJECT_VERSION_MAJOR)
endef

###############################################################################
# Install static libraries (the project target shall be a library).
###############################################################################

define INSTALL_PROJECT_STATIC_LIBRARIES
	@$(call print-install,$(TARGET_NAME),$(STATIC_LIB_NAME),$(INSTALL_LIBDIR))
	$(Q)install -d -m 755 $(INSTALL_LIBDIR)
	$(Q)install -m 644 $(TARGET_STATIC_LIB_NAME) $(INSTALL_LIBDIR)
endef

###############################################################################
# Install all .hpp files (the project target shall be a library).
###############################################################################

define INSTALL_PROJECT_HEADERS
	@$(call HELPER_INSTALL_CXX_HEADER_FILES,$(P)/include,$(INSTALL_INCLUDEDIR))
	@$(call HELPER_INSTALL_CXX_HEADER_FILES,$(P)/src,$(INSTALL_INCLUDEDIR))
endef

###############################################################################
# Create a MacOS application bundle
###############################################################################

define CREATE_MACOS_INFO_PLIST
	@$(ECHO) "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" > $(OBJS_ROOT_DIR)/$1
	@$(ECHO) "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">" >> $(OBJS_ROOT_DIR)/$1
	@$(ECHO) "<plist version=\"1.0\">" >> $(OBJS_ROOT_DIR)/$1
	@$(ECHO) "<dict>" >> $(OBJS_ROOT_DIR)/$1
	@$(ECHO) "    <key>CFBundleExecutable</key>" >> $(OBJS_ROOT_DIR)/$1
	@$(ECHO) "    <string>${TARGET}</string>" >> $(OBJS_ROOT_DIR)/$1
	@$(ECHO) "    <key>CFBundleIdentifier</key>" >> $(OBJS_ROOT_DIR)/$1
	@$(ECHO) "    <string>com.${APPLE_IDENTIFIER}.${TARGET}</string>" >> $(OBJS_ROOT_DIR)/$1
	@$(ECHO) "    <key>CFBundleInfoDictionaryVersion</key>" >> $(OBJS_ROOT_DIR)/$1
	@$(ECHO) "    <string>6.0</string>" >> $(OBJS_ROOT_DIR)/$1
	@$(ECHO) "    <key>CFBundleName</key>" >> $(OBJS_ROOT_DIR)/$1
	@$(ECHO) "    <string>${TARGET}</string>" >> $(OBJS_ROOT_DIR)/$1
	@$(ECHO) "    <key>CFBundlePackageType</key>" >> $(OBJS_ROOT_DIR)/$1
	@$(ECHO) "    <string>APPL</string>" >> $(OBJS_ROOT_DIR)/$1
	@$(ECHO) "    <key>CFBundleIconFile</key>" >> $(OBJS_ROOT_DIR)/$1
	@$(ECHO) "    <string>${TARGET}.icns</string>" >> $(OBJS_ROOT_DIR)/$1
	@$(ECHO) "</dict>" >> $(OBJS_ROOT_DIR)/$1
	@$(ECHO) "</plist>" >> $(OBJS_ROOT_DIR)/$1
endef

###############################################################################
# Create a MacOS application bundle
###############################################################################

define CREATE_MACOS_APP_BUNDLE
	@$(call print-to,Generating bundle application,$(TARGET_NAME),$1)
	$(Q)$(RM) $(OBJS_ROOT_DIR)/$(TARGET_NAME)$(EXT) 2> /dev/null
	$(Q)$(MKDIR) $(OBJS_ROOT_DIR)/$(TARGET_NAME)$(EXT)/Contents/{MacOS,Resources}
	$(Q)$(CP) $(OBJS_ROOT_DIR)/$(TARGET_NAME) $(OBJS_ROOT_DIR)/$(TARGET_NAME)$(EXT)/Contents/MacOS
	$(Q)$(CP) $(MACOS_BUNDLE_ICON) $(OBJS_ROOT_DIR)/$(TARGET_NAME)$(EXT)/Contents/Resources/$(TARGET_NAME).icns
	$(Q)cp -r data/* $(OBJS_ROOT_DIR)/$(TARGET_NAME)$(EXT)/Contents/Resources
	@$(call CREATE_MACOS_INFO_PLIST,$(TARGET_NAME)$(EXT)/Contents/Info.plist)
endef

###############################################################################
# Create the pkg-config file (if the project is a library)
###############################################################################

define CREATE_PKG_CONFIG
	@$(call print-to,Generating pkg-config,$(TARGET_NAME),$(abspath $1))
	@$(ECHO) "prefix=$(PREFIX)" > $1
	@$(ECHO) "exec_prefix=$(PREFIX)" >> $1
	@$(ECHO) "libdir=$(LIBDIR)" >> $1
	@$(ECHO) "includedir=$(INCLUDEDIR)/$(PROJECT_NAME)/$(PROJECT_VERSION)" >> $1
	@$(ECHO) "Name: lib$(TARGET_NAME)" >> $1
	@$(ECHO) "Description: $(DESCRIPTION)" >> $1
	@$(ECHO) "Version: $(PROJECT_VERSION)" >> $1
	@$(ECHO) "Libs: -L$$""{libdir} $(strip -l$(TARGET_NAME) $(SHORT_INTERNAL_LIBS) $(NOT_PKG_LIBS) $(PKGCFG_LIBS))" >> $1
	@$(ECHO) "Cflags: -I$$""{includedir}" >> $1
endef

###############################################################################
# Create the exa.html file for ExaequOS
###############################################################################

define CREATE_EXAEQUOS_HTML
	@$(call print-to,"Generating","$(TARGET_NAME)","$1","")
	@$(ECHO) "<!doctype html>" > $(INSTALL_BINDIR)/$1
	@$(ECHO) "<html lang=\"en-us\">" >> $(INSTALL_BINDIR)/$1
	@$(ECHO) "<head>" >> $(INSTALL_BINDIR)/$1
	@$(ECHO) "  <meta charset=\"utf-8\">" >> $(INSTALL_BINDIR)/$1
	@$(ECHO) "  <meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">" >> $(INSTALL_BINDIR)/$1
	@$(ECHO) "  <title>Emscripten-Generated Code</title>" >> $(INSTALL_BINDIR)/$1
	@$(ECHO) "  <style>" >> $(INSTALL_BINDIR)/$1
 	@$(ECHO) "   html, body {" >> $(INSTALL_BINDIR)/$1
 	@$(ECHO) "     margin : 0;" >> $(INSTALL_BINDIR)/$1
	@$(ECHO) "      padding: 0;" >> $(INSTALL_BINDIR)/$1
	@$(ECHO) "      border: none;" >> $(INSTALL_BINDIR)/$1
	@$(ECHO) "      width: 100%;" >> $(INSTALL_BINDIR)/$1
	@$(ECHO) "      height: 100%;" >> $(INSTALL_BINDIR)/$1
	@$(ECHO) "    }" >> $(INSTALL_BINDIR)/$1
	@$(ECHO) "  </style>" >> $(INSTALL_BINDIR)/$1
	@$(ECHO) "</head>" >> $(INSTALL_BINDIR)/$1
	@$(ECHO) "<body>" >> $(INSTALL_BINDIR)/$1
	@$(ECHO) "  <script async type=\"text/javascript\" src=\"$(TARGET_NAME).js\"></script>" >> $(INSTALL_BINDIR)/$1
	@$(ECHO) "</body>" >> $(INSTALL_BINDIR)/$1
	@$(ECHO) "</html>" >> $(INSTALL_BINDIR)/$1
endef

###############################################################################
# Create Doxyfile files (TODO: update license in footer.html.
# FIXME linux path make failed sed ie LOGO)
###############################################################################

define RULE_CREATE_DOXYFILES
    $(MKDIR) = $(dir $(GENERATED_DOXYGEN_DIR))
	$(CP) $(PATH_MYMAKEFILE_DOXYFILE)/. $(GENERATED_DOXYGEN_DIR)
	find $(GENERATED_DOXYGEN_DIR) -type f -exec sed -i 's/%VERSION/$(PROJECT_VERSION)/g' {} \;
	find $(GENERATED_DOXYGEN_DIR) -type f -exec sed -i 's/%TARGET/$(TARGET_NAME)/g' {} \;
	find $(GENERATED_DOXYGEN_DIR) -type f -exec sed -i 's/%PROJECT/$(PROJECT_NAME)/g' {} \;
	find $(GENERATED_DOXYGEN_DIR) -type f -exec sed -i 's/%DESCRIPTION/$(subst /,\/,$(TARGET_DESCRIPTION))/g' {} \;
	find $(GENERATED_DOXYGEN_DIR) -type f -exec sed -i 's/%GENDOC/$(subst /,\/,$(GENERATED_DOXYGEN_DIR))/g' {} \;
	find $(GENERATED_DOXYGEN_DIR) -type f -exec sed -i 's/%LOGO/$(subst /,\/,$(PATH_PROJECT_LOGO))/g' {} \;
	find $(GENERATED_DOXYGEN_DIR) -type f -exec sed -i 's/%INPUT/$(subst /,\/,$(DOXYGEN_INPUTS))/g' {} \;
	$(MV) $(GENERATED_DOXYGEN_DIR)/Doxyfile $(abspath $(P)/Doxyfile)
endef

###############################################################################
# git clone non-recursively a project with depth history set to 1.
###############################################################################

define GIT_CLONE_NO_RECURSE
	$(MKDIR) "$1";
	if [ ! -d "$1/$3" ]; then \
		source $(SCRIPT_DOWNLOAD_THIRDPARTS) $(PROJECT_NAME) && \
		cd $1 && cloning_non_recurse $2/$3; \
	else \
		echo "Already cloned"; \
    fi
endef

###############################################################################
# git clone a project recursively a project with depth history set to 1.
###############################################################################

define GIT_CLONE_RECURSE
	$(MKDIR) "$1";
	if [ ! -d "$1/$3" ]; then \
		source $(SCRIPT_DOWNLOAD_THIRDPARTS) $(PROJECT_NAME) && \
		cd $1 && cloning $2/$3; \
	else \
		echo "Already cloned"; \
    fi
endef

###############################################################################
#? Compile the standalone application or static/shared libraries.
###############################################################################

.PHONY: all
all:: about compiler-info creating-build-folder $(DIRS_WITH_MAKEFILE) $(TARGET_SHARED_LIB_NAME) $(TARGET_STATIC_LIB_NAME) $(TARGET_PKG_FILE_NAME) $(TARGET_BINARY_NAME)

###############################################################################
# Link sources as static library.
###############################################################################

ifdef LIB_OBJS
$(TARGET_STATIC_LIB_NAME): $(LIB_OBJS) $(INTERNAL_LIBS) $(LIST_MYMAKEFILES) $(DIRS_WITH_MAKEFILE)
	@$(call print-to,"Creating static library","$(TARGET_NAME)","$@")
ifdef THIRDPART_LIBS
	@$(call extract-objs-from-static-libs)
endif
	$(Q)$(CD) $(abspath $(OBJS_ROOT_DIR)) && \
    $(AR) $(ARFLAGS) $@ $(patsubst $(abspath $(BUILD_DIR)/objs)/%,%,$(abspath $(LIB_OBJS) $(EXTRACTED_OBJS))) && \
    ranlib $@
endif

###############################################################################
# Link sources as shared library.
###############################################################################

ifdef LIB_OBJS
$(TARGET_SHARED_LIB_NAME): | $(TARGET_STATIC_LIB_NAME)
$(TARGET_SHARED_LIB_NAME): $(LIB_OBJS) $(INTERNAL_LIBS) $(LIST_MYMAKEFILES) $(DIRS_WITH_MAKEFILE)
	@$(call print-to,"Creating shared library","$(TARGET_NAME)","$@")
	$(Q)$(COMPILER) $(LDFLAGS) $(DLL_LDFLAGS) -o $@ $(abspath $(LIB_OBJS)) $(LINKER_ALL_LOAD) $(THIRDPART_LIBS) $(INTERNAL_LIBS) $(NOT_PKG_LIBS) $(PKGCFG_LIBS) $(LINKER_NO_ALL_LOAD) $(LINKER_FLAGS) $(USER_LDFLAGS)
endif

###############################################################################
# Generate the pkg-config file.
###############################################################################

ifdef TARGET_PKG_FILE_NAME
$(TARGET_PKG_FILE_NAME): | $(TARGET_SHARED_LIB_NAME)
$(TARGET_PKG_FILE_NAME): $(LIB_OBJS) $(INTERNAL_LIBS) $(LIST_MYMAKEFILES) $(DIRS_WITH_MAKEFILE)
	@$(call CREATE_PKG_CONFIG,$(TARGET_PKG_FILE_NAME))
endif

###############################################################################
# Link sources to standalone application.
###############################################################################

ifdef OBJS
$(TARGET_BINARY_NAME): $(OBJS) $(INTERNAL_LIBS) $(LIST_MYMAKEFILES) $(DIRS_WITH_MAKEFILE)
	@$(call print-to,Linking,$(TARGET_NAME),$@)
	$(Q)$(COMPILER) $(LDFLAGS) $(OBJS) $(THIRDPART_OBJS) -o $(TARGET_BINARY_NAME) $(THIRDPART_LIBS) $(INTERNAL_LIBS) $(NOT_PKG_LIBS) $(PKGCFG_LIBS) $(LINKER_FLAGS)
ifdef STRIP
	@$(call print-to,Stripping,$(TARGET_NAME),$@)
	$(Q)$(CD) $(OBJS_ROOT_DIR) && $(STRIP) $(TARGET_BINARY_NAME)
endif
ifdef BUILD_MACOS_APP_BUNDLE
	$(Q)$(call CREATE_MACOS_APP_BUNDLE,$(TARGET_BINARY_NAME)$(EXT))
endif
endif

###############################################################################
# Compile C++ sources. Note: abspath is needed for gcov.
###############################################################################

$(OBJS_ROOT_DIR)/%.o: %.cpp $(OBJS_ROOT_DIR)/%.d $(LIST_MYMAKEFILES) $(INTERNAL_LIBS)
	@$(call progress-in,Compiling C++,$(TARGET_NAME),$<)
	$(Q)$(CXX) $(CXX_STANDARD) $(DEPFLAGS) $(CXXFLAGS) $(PKGCFG_CFLAGS) $(OPTIM_FLAGS) $(DEFINES) $(INCLUDES) $(USER_CXXFLAGS) -c $(abspath $<) -o $(abspath $@)
	@$(POSTCOMPILE)

###############################################################################
# Compile C sources. Note: abspath is needed for gcov.
###############################################################################

$(OBJS_ROOT_DIR)/%.o: %.c $(OBJS_ROOT_DIR)/%.d $(LIST_MYMAKEFILES) $(INTERNAL_LIBS)
	@$(call progress-in,Compiling C,$(TARGET_NAME),$<)
	$(Q)$(CC) $(DEPFLAGS) $(CCFLAGS) $(PKGCFG_CFLAGS) $(OPTIM_FLAGS) $(DEFINES) $(INCLUDES) $(USER_CCFLAGS) -c $(abspath $<) -o $(abspath $@)
	@$(POSTCOMPILE)

###############################################################################
# Git clone thirdy-part libraries helping for the debug.
###############################################################################

ifdef USE_BACKWARD
$(TARGET_BINARY_NAME): $(DOWNLOAD_DIR)/backward-cpp/backward.cpp
$(TARGET_STATIC_LIB_NAME): $(DOWNLOAD_DIR)/backward-cpp/backward.cpp
$(TARGET_SHARED_LIB_NAME): $(DOWNLOAD_DIR)/backward-cpp/backward.cpp
$(DOWNLOAD_DIR)/backward-cpp/backward.cpp:
	@$(call GIT_CLONE_NO_RECURSE,$(DOWNLOAD_DIR),bombela,backward-cpp)
endif

ifdef USE_DEBUG_MACRO
$(TARGET_BINARY_NAME): $(DOWNLOAD_DIR)/dbg-macro/dbg.h
$(TARGET_STATIC_LIB_NAME): $(DOWNLOAD_DIR)/dbg-macro/dbg.h
$(TARGET_SHARED_LIB_NAME): $(DOWNLOAD_DIR)/dbg-macro/dbg.h
$(DOWNLOAD_DIR)/dbg-macro/dbg.h:
	@$(call GIT_CLONE_NO_RECURSE,$(DOWNLOAD_DIR),sharkdp,dbg-macro)
endif

###############################################################################
# Compile sub Makefiles we depend on.
###############################################################################

ifdef DIRS_WITH_MAKEFILE
.PHONY: $(DIRS_WITH_MAKEFILE)
$(DIRS_WITH_MAKEFILE):
	$(Q)$(MAKE) --no-print-directory $(addprefix --directory=,$@) all
endif

###############################################################################
# Create a build folder in which compiled files will be stored.
###############################################################################

.PHONY: creating-build-folder
creating-build-folder:
	@$(call print-out,Create build folder,$(TARGET_NAME),$(GENERATED_HEADER_PROJECT_INFO))
	@$(MKDIR) $(addprefix $(OBJS_ROOT_DIR)/,$(BUILD_FOLDERS)) $(DOWNLOAD_DIR)

###############################################################################
# Generate a header file with the project information.
###############################################################################

$(LIB_OBJS) $(OBJS): | $(GENERATED_HEADER_PROJECT_INFO)
$(GENERATED_HEADER_PROJECT_INFO): $(LIST_MYMAKEFILES)
	@$(call print-out,Generating projet info,$(TARGET_NAME),$(GENERATED_HEADER_PROJECT_INFO))
	@$(MKDIR) $(GENERATION_DIR)
	@$(ECHO) "VERSION=$(PROJECT_VERSION)" > $(GENERATION_DIR)/$(PROJECT_INFO).txt
	@$(ECHO) "PROJECT=$(PROJECT_NAME)" >> $(GENERATION_DIR)/$(PROJECT_INFO).txt
	@$(ECHO) "TARGET=$(TARGET_NAME)" >> $(GENERATION_DIR)/$(PROJECT_INFO).txt
	@$(ECHO) "DATA_PATHS=$(SEARCH_DATA_PATHS)" >> $(GENERATION_DIR)/$(PROJECT_INFO).txt
	@$(ECHO) "TEMPDIR=$(PROJECT_TEMP_DIR)" >> $(GENERATION_DIR)/$(PROJECT_INFO).txt
	@$(ECHO) "COMPILATION_MODE=$(COMPILATION_MODE)" >> $(GENERATION_DIR)/$(PROJECT_INFO).txt
	@$(SCRIPT_CONFIG) $(GENERATION_DIR)/$(PROJECT_INFO)

###############################################################################
# Auto-Dependency Generation
###############################################################################

.PRECIOUS: $(OBJS_ROOT_DIR)/%.d
$(OBJS_ROOT_DIR)/%.d: ;

-include $(patsubst %,%.d,$(basename $(OBJS) $(LIB_OBJS)))

###############################################################################
#? Download external github code source needed by this project.
###############################################################################
# FIXME: find something smarter for installing Exaequos packages
.PHONY: download-external-libs
download-external-libs:
	@$(call print-to,Downloading ExaequOS packages,$(TARGET_NAME),$(THIRDPART_DIR))
ifdef EXAEQUOS
	$(Q)$(RM) exapkgs 2> /dev/null
	$(Q)epm.py install exa-wayland glfw raylib
endif
	@$(call print-to,Downloading third-party libs,$(TARGET_NAME),$(THIRDPART_DIR))
ifneq ("$(wildcard $(USER_SCRIPT_DOWNLOAD_THIRDPARTS))","")
	$(Q)cd $(THIRDPART_DIR) && $(SCRIPT_DOWNLOAD_THIRDPARTS) $(PROJECT_NAME) $(USER_SCRIPT_DOWNLOAD_THIRDPARTS)
else
	$(Q)$(ECHO_COLOR) "  $(YELLOW):( $(CYAN)Missing file $(USER_SCRIPT_DOWNLOAD_THIRDPARTS) $(YELLOW):("
	$(Q)$(ECHO_COLOR) "  $(YELLOW):) $(CYAN)Or no third-party libs to download ! $(YELLOW):)$(COLOR_DEFAULT)"
endif

###############################################################################
#? Compile external projects needed.
###############################################################################

.PHONY: compile-external-libs
compile-external-libs:
	@$(call print-to,Compiling third-party libs,$(TARGET_NAME),$(THIRDPART_DIR))
ifneq ("$(wildcard $(USER_SCRIPT_COMPILE_THIRDPARTS))","")
	$(Q)$(CD) $(THIRDPART_DIR) && $(SCRIPT_COMPILE_THIRDPARTS) $(PROJECT_NAME) $(USER_SCRIPT_COMPILE_THIRDPARTS) $(OS) $(CC) $(CXX)
else
	$(Q)$(ECHO_COLOR) "  $(YELLOW):( $(CYAN)Missing file $(USER_SCRIPT_COMPILE_THIRDPARTS) $(YELLOW):("
	$(Q)$(ECHO_COLOR) "  $(YELLOW):) $(CYAN)Or no third-party libs to compile ! $(YELLOW):)$(COLOR_DEFAULT)"
endif

###############################################################################
#? Clean the project target build and other Makefiles cleaning.
###############################################################################

.PHONY: clean
clean::
ifeq ($(abspath .), $(abspath $(P)))
	@$(call print-simple,Cleaning project,$(PROJECT_NAME))
else
	@$(call print-simple,Cleaning target,$(TARGET_NAME))
endif
	$(Q)$(RM) $(OBJS_ROOT_DIR) 2> /dev/null
ifdef DIRS_WITH_MAKEFILE
	$(Q)for d in $(DIRS_WITH_MAKEFILE); do \
	$(MAKE) --no-print-directory $(addprefix --directory=,$$d) clean; \
	done
endif

###############################################################################
#? Clean the build folder and generated documentation.
###############################################################################

.PHONY: veryclean
veryclean::
	@$(call print-simple,Cleaning project,$(PROJECT_NAME))
	$(Q)$(RM) $(BUILD_DIR) $(GENERATED_DOXYGEN_DIR) $(dir $(GPROF_ANALYSIS) $(COVERAGE_RAPPORT)) 2> /dev/null

###############################################################################
#? Install the project artifacts on the operating system
###############################################################################

.PHONY: install
install::
ifdef INSTALL_LOCAL_PROJECT_DATA_DIR
	$(Q)install -d -m 755 $(INSTALL_LOCAL_PROJECT_DATA_DIR)
endif
ifdef DIRS_WITH_MAKEFILE
	$(Q)for d in $(DIRS_WITH_MAKEFILE); do \
	$(MAKE) --no-print-directory $(addprefix --directory=,$$d) install; \
	done
endif
ifdef TARGET_BINARY_NAME
	$(Q)$(call INSTALL_PROJECT_BINARY)
endif
ifneq ($(OS),Emscripten)
  ifdef TARGET_SHARED_LIB_NAME
	$(Q)$(call INSTALL_PROJECT_SHARED_LIBRARIES)
  endif
  ifdef TARGET_STATIC_LIB_NAME
	$(Q)$(call INSTALL_PROJECT_STATIC_LIBRARIES)
  endif
  ifdef TARGET_PKG_FILE_NAME
	$(Q)$(call INSTALL_PROJECT_PKG_CONFIG)
  endif
# FIXME: Actuellement on copie plusieurs fois les memes fichiers. Seul la regle
# du Makefile a la racine devrait le faire mais actuellement il faut compiler une
# lib. Si on compile une application et des sous lib, on ne compie pas les fichiers
# headers.
#  ifeq ($(abspath .), $(abspath $(P)))
	$(Q)$(call INSTALL_PROJECT_HEADERS)
#  endif
  ifeq ($(abspath .), $(abspath $(P)))
	$(Q)$(call INSTALL_PROJECT_DOC)
	$(Q)$(call INSTALL_PROJECT_DATA)
	$(Q)$(call INSTALL_PROJECT_INFO_FILES)
  endif
endif

###############################################################################
#? Generate the code source documentation with doxygen.
###############################################################################

.PHONY: doc
doc:
	@$(call RULE_CREATE_DOXYFILES)
	$(Q)doxygen Doxyfile
	$(Q)xdg-open $(DOXYGEN_INDEX_HTML) & >/dev/null

###############################################################################
#? Compress project sources without in the goal to backup the code or share it.
# This will ignore .git, build, generated documentations.
# Possible tarball name conflict with existing one is managed: if a tarball
# already exists, the older will stay intact and a new one is created.
###############################################################################

.PHONY: tarball
tarball:
	$(Q)$(SCRIPT_TAR_PROJECT) $(abspath $(P)) $(TARGET_NAME) $(THIRDPART_DIR) $(PROJECT_VERSION)

###############################################################################
#? Display the compiler version and information.
###############################################################################

.PHONY: compiler-info
compiler-info:
ifeq ($(abspath .), $(abspath $(P)))
	@$(call print-simple,About compiler,$(COMPILER))
	$(Q)$(COMPILER) --version
  ifeq ($(OS),Emscripten)
    ifdef EXAEQUOS
	@$(call print-info,Compiling with emscripten-exa for ExaequOS)
    else
	@$(call print-info,Compiling with emscripten)
    endif
  endif
endif

###############################################################################
#? Display the description of the project.
###############################################################################

.PHONY: about
about:
	@$(call print-to,Compiling target,$(TARGET_NAME),$(TARGET_DESCRIPTION), [$(COMPILATION_MODE)])

###############################################################################
#? Check if your project is harden
###############################################################################

.PHONY: check-harden
check-harden: $(TARGET_NAME)
	$(Q)hardening-check ./$(OBJS_ROOT_DIR)/$(TARGET_NAME); echo -n ""
ifeq ($(COMPILATION_MODE),debug)
	@$(ECHO) " Project compiled in debug mode"
endif

###############################################################################
#? Run the binary with optional arguments. For example make run -- foo --help
###############################################################################

ifdef TARGET_BINARY_NAME
# Pass extra arguments to your applications
ifeq (run,$(firstword $(MAKECMDGOALS)))
    RUN_ARGS := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
endif

.PHONY: run
run: all
	$(Q)$(RUN) $(TARGET_BINARY_NAME) $(RUN_ARGS) && $(ECHO_COLOR) "$(GREEN)  Error code: $$?$(COLOR_DEFAULT)" || $(ECHO_COLOR) "$(RED)  Error code: $$?$(COLOR_DEFAULT)" 

# Do not interprete arguments as makefile rule
ifdef RUN_ARGS
.PHONY: $(RUN_ARGS)
$(RUN_ARGS):;@:
endif
endif

###############################################################################
#? Call unit-tests with code coverage.
###############################################################################

.PHONY: check tests
check:
tests:
	@$(call print-simple,"Compiling unit tests")
ifneq ($(wildcard $(PROJECT_TESTS)/.),)
	@$(MAKE) -C $(PROJECT_TESTS) coverage
else
	@$(MAKE) coverage
endif

###############################################################################
#? Launch the executable with address sanitizer (if enabled).
###############################################################################

.PHONY: asan
asan: $(TARGET_BINARY_NAME)
	$(Q)$(SANITIZER) ./$(TARGET_BINARY_NAME) 2>&1 | ./$(THIRDPART_DIR)/asan_symbolize.py

###############################################################################
#? Launch the executable with gprof.
###############################################################################

.PHONY: check-compiled-with-gprof
check-compiled-with-gprof:
	@$(call print-simple,"Profiling your target",$(TARGET_NAME))
	@if [ "$(USE_GPROF)" = "" ]; then \
		$(ECHO_COLOR) "  $(RED)You have not compiled your project with gprof"; \
        $(ECHO_COLOR) "  Please add in your Makefile: USE_GPROF := 1"; \
        $(ECHO_COLOR) "  and recompile it again!$(COLOR_DEFAULT)"; \
        exit 1; \
    fi

.PHONY: gprof
gprof: check-compiled-with-gprof run
	$(Q)$(MKDIR) $(GPROF_ANALYSIS_FOLDER)
	$(Q)mv gmon.out $(GPROF_ANALYSIS_FOLDER)
	$(Q)gprof $(TARGET_BINARY_NAME) $(GPROF_ANALYSIS_FOLDER)/gmon.out > $(GPROF_ANALYSIS)
	@$(ECHO_COLOR) "$(GREEN)  You can read the report: $(GPROF_ANALYSIS)"

###############################################################################
#? Generate the code coverage html rapport.
###############################################################################

.PHONY: coverage
coverage: all
	@$(call print-to,Running,$(PROJECT_NAME),$(abspath $(TARGET_BINARY_NAME)))
	$(Q)$(TARGET_BINARY_NAME)
	@$(call print-to,Coverage report,$(abspath $(TARGET_BINARY_NAME)),$(COVERAGE_RAPPORT))
	$(Q)$(MKDIR) $(dir $(COVERAGE_RAPPORT))
	$(Q)gcovr --root $(P) --object-directory $(OBJS_ROOT_DIR) --html-details $(COVERAGE_RAPPORT) -e '/usr/*' --exclude-directories $(THIRDPART_DIR)
	$(Q)xdg-open $(COVERAGE_RAPPORT) >/dev/null

###############################################################################
#? Create a tarball for Coverity Scan a static analysis of code.
# For using Coverity Scan service, you have to download and install a gcc
# wrapper and compile your project back. A tarball should be created and you
# have to upload it to their webpage. See https://scan.coverity.com/ for more
# information.
###############################################################################

.PHONY: coverity-scan
coverity-scan: clean
	$(Q)$(RM) $(TARGET_BINARY_NAME).tgz cov-int 2> /dev/null
	$(Q)cov-build --dir cov-int $(MAKE) && tar czvf $(TARGET_BINARY_NAME).tgz cov-int

###############################################################################
#? Create an uploadable tarball for the OpenSuse Build Service.
###############################################################################

.PHONY: obs
obs:
	$(Q)$(SCRIPT_OBS)

###############################################################################
#? Show this help.
###############################################################################

.PHONY: help
help:
	@$(ECHO) 'Usage:'
	@$(ECHO) '  [VERBOSE=1] make [flags...] <target>'
	@$(ECHO) ''
	@$(ECHO) 'You can override the following flags:'
	@$(ECHO) ''
	@awk '/^#\? /{ comment = substr($$0,4) } comment && \
    /^ *[a-zA-Z][a-zA-Z0-9_-]+ ?\?=/ \
    { \
       printf "  \033[36m%s|\033[0m%s\n    \033[00mDefault value: \033[32m%s", $$1, comment, $$3; \
       for(i=4;i<=NF;i++){printf " %s", $$i} printf "\n" \
    }' $(MAKEFILE_LIST) | column -t -s '|'
	@$(ECHO_COLOR) -e '\033[00m'
	@$(ECHO) 'Targets:'
	@$(ECHO) ''
	@awk '/^#\? /{ comment = substr($$0,4) } comment && \
    /^[a-zA-Z][a-zA-Z0-9_-]+:/{ printf "  \033[36m%s|\033[0m%s\n", $$1, comment }' \
    $(MAKEFILE_LIST) | sort | column -t -s '|'
	@$(ECHO_COLOR) -e '\033[00m'
