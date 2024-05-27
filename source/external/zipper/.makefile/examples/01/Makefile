PROJECT = TutorialMyMakefile
TARGET = HelloWorld01
DESCRIPTION = Tutorial 01 for MyMakefile: Hello World project with unit tests
BUILD_TYPE = release

P := ../..
M := $(P)
include $(M)/Makefile.header

OBJS += foo.o main.o
DEFINES += -DFOO -UBAR
VPATH += src
INCLUDES += -Isrc

.PHONY: all
all: $(TARGET)

.PHONY: check
check:
	@$(call print-simple,"Compiling unit tests")
	@$(MAKE) -C tests coverage

.PHONY: veryclean
veryclean: clean
	@(cd tests && $(MAKE) -s clean)

include $(M)/Makefile.footer
