PROJECT = TutorialMyMakefile
TARGET = HelloWorld01-UnitTests
DESCRIPTION = Unit Tests for the Hello World project
# Compile using coverage
USE_COVERAGE = 1
# Activate cassert
BUILD_TYPE = debug

P := ../../..
M := $(P)
include $(M)/Makefile.header

# foo.o is compiled object from ../src/foo.c
OBJS += unit-tests.o

# For allowing #include "src/foo.cpp"
INCLUDES += -I..

.PHONY: all
all: $(TARGET)

include $(M)/Makefile.footer
