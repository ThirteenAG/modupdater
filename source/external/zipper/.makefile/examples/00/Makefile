PROJECT = TutorialMyMakefile
TARGET = HelloWorld00
DESCRIPTION = Tutorial 00 for MyMakefile: Hello World project
BUILD_TYPE = release

P := ../..
M := $(P)
include $(M)/Makefile.header

OBJS += main.o
DEFINES += -DFOO -UBAR
VPATH += src
INCLUDES += -Isrc

.PHONY: all
all: $(TARGET)

include $(M)/Makefile.footer
