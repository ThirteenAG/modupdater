#!/bin/bash -e
###############################################################################
### This script is called by (cd .. && make compile-external-libs). It will
### compile thirdparts cloned previously with make download-external-libs.
###
### To avoid pollution, these libraries are not installed in your operating
### system (no sudo make install is called). As consequence, you have to tell
### your project ../Makefile where to find their files. Here generic example:
###     INCLUDES += -I$(THIRDPART)/thirdpart1/path1
###        for searching heeder files.
###     VPATH += $(THIRDPART)/thirdpart1/path1
###        for searching c/c++ files.
###     THIRDPART_LIBS += $(abspath $(THIRDPART)/libXXX.a))
###        for linking your project against the lib.
###     THIRDPART_OBJS += foo.o
###        for inking your project against this file iff THIRDPART_LIBS is not
###        used (the path is not important thanks to VPATH).
###
### The last important point to avoid polution, better to compile thirdparts as
### static library rather than shared lib to avoid telling your system where to
### find them when you'll start your application.
###
### Input of this script:
### $1: architecture (i.e. Linux, Darwin, Windows, Emscripten)
### $2: target (your application name)
### $3: CC (C compiler)
### $4: CXX (C++ compiler)
###############################################################################

function fatal
{
    echo -e "\033[35mFailure! $1\033[00m"
    exit 1
}

### $1 is given by ../Makefile and refers to the current architecture.
ARCHI="$1"
if [ "$1" == "" ]; then
      fatal "Expected one argument. Select the architecture: Linux, Darwin or Windows"
fi

### $2 is given by ../Makefile and refers to the current target.
TARGET="$2"
if [ "$TARGET" == "" ]; then
    fatal "Define the binary target as $2"
fi

### Compilation flags if not using Emscripten
if [ "$ARCHI" != "Emscripten" ]; then
    CC="$3"
    CXX="$4"
    if [ "$3" == "" -o "$4" == "" ]; then
        fatal "Define compiler CC as $3 and CXX as $4"
    fi
    export CXX=$CXX
    export CC=$CC
fi

### Number of CPU cores
NPROC=
if [ "$ARCHI" == "Darwin" ]; then
    NPROC=`sysctl -n hw.logicalcpu`
else
    NPROC=`nproc`
fi

function print-compile
{
    echo -e "\033[35m*** Compiling:\033[00m \033[36m$TARGET\033[00m <= \033[33m$1\033[00m"
    if [ ! -e $1 ];
    then
        echo "Failed compiling external/$TARGET: directory does not exist"
    fi
}

function call-configure
{
    if [ "$ARCHI" == "Emscripten" ]; then
        emconfigure ./configure $*
    else
        ./configure $*
    fi
}

function call-cmake
{
    if [ "$ARCHI" == "Emscripten" ]; then
        emcmake cmake $*
    else
        cmake $*
    fi
}

function call-make
{
    if [ "$ARCHI" == "Emscripten" ]; then
        VERBOSE=1 emmake make -j$NPROC $*
    else
        VERBOSE=1 make -j$NPROC $*
    fi
}