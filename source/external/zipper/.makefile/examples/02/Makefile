###################################################
# Project definition
#
PROJECT = TutorialMyMakefile
TARGET = HelloWorld02
DESCRIPTION = Tutorial 02 for MyMakefile: Hello libraries
BUILD_TYPE = release

###################################################
# Location of the project directory and Makefiles
#
P := ../..
M := $(P)
include $(M)/Makefile.header

###################################################
# Inform Makefile where to find header files
#
INCLUDES += -Iinclude

###################################################
# Inform Makefile where to find *.cpp and *.o files
#
VPATH += src src/foo/$(BUILD) src/bar/$(BUILD)

###################################################
# Make the list of compiled files for the standalone
# application.
#
OBJS += main.o

###################################################
# Thirdpart libraries
#
LIBFOO=$(abspath src/foo/$(BUILD)/libfoo.a.0.1.0)
LIBBAR=$(abspath src/bar/$(BUILD)/libbar.a.0.1.0)
THIRDPART_LIBS += $(LIBFOO) $(LIBBAR)

###################################################
# The static library, shared library, pkg-config file.
#
.PHONY: all
all: $(TARGET)

$(LIBFOO):
	$(MAKE) -C src/foo all

$(LIBBAR):
	$(MAKE) -C src/bar all

###################################################
# Clean all folders
#
.PHONY: veryclean
veryclean: clean
	$(MAKE) -C src/foo clean
	$(MAKE) -C src/bar clean

###################################################
# Sharable informations between all Makefiles
#
include $(M)/Makefile.footer
