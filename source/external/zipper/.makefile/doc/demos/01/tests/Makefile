PROJECT_NAME := TutorialMyMakefile
PROJECT_VERSION := 0.1.0
TARGET_NAME := UnitTest-HelloWorld01
TARGET_DESCRIPTION := Unit Tests for the Hello World project
COMPILATION_MODE := test

P := ..
M := ../../../..
BUILD := ../../build
include $(M)/project/Makefile

INCLUDES := $(P)/src
SRC_FILES := $(P)/src/foo.cpp unit-tests.cpp
DEFINES += -DFOO -UBAR

include $(M)/rules/Makefile
