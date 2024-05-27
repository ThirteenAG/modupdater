###################################################
# Project definition
#
PROJECT = TutorialMyMakefile
TARGET = Foo
DESCRIPTION = Tutorial 02 for MyMakefile: Hello libraries
BUILD_TYPE = release

###################################################
# Location of the project directory and Makefiles
#
P := ../../../..
M := $(P)
include $(M)/Makefile.header

###################################################
# Inform Makefile where to find header files
#
INCLUDES += -I../../include

###################################################
# Inform Makefile where to find *.cpp and *.o files
#
VPATH += .

###################################################
# Make the list of compiled files for the library
#
LIB_OBJS += foo.o

###################################################
# The static library, shared library, pkg-config file.
#
.PHONY: all
all: $(STATIC_LIB_TARGET) $(SHARED_LIB_TARGET) $(PKG_FILE)

###################################################
# Sharable informations between all Makefiles
#
include $(M)/Makefile.footer
