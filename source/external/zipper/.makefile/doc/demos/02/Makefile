PROJECT_NAME := TutorialMyMakefile
PROJECT_VERSION := 0.1.0
TARGET_NAME := HelloWorld02
TARGET_DESCRIPTION := Hello Word
COMPILATION_MODE := release
#FORCE_LOWER_CASE := 1

P := .
M := ../../..
BUILD := ../../build
include $(M)/project/Makefile

INCLUDES := include
SRC_FILES := src/main.cpp
DIRS_WITH_MAKEFILE := src/bar src/foo
INTERNAL_LIBS := foo bar

include $(M)/rules/Makefile

# Optional: to force compilation order of libraries
src/bar: src/foo
