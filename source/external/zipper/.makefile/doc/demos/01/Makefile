PROJECT_NAME := TutorialMyMakefile
PROJECT_VERSION := 0.1.0
TARGET_NAME := HelloWorld01
TARGET_DESCRIPTION := Hello Word
COMPILATION_MODE := release

P := .
M := ../../..
BUILD := ../../build
include $(M)/project/Makefile

INCLUDES := src
SRC_FILES := src/foo.cpp src/main.cpp
DEFINES += -DFOO -UBAR

include $(M)/rules/Makefile
