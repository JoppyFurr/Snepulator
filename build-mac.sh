#!/bin/sh

# Select compiler
COMPILER=${1:-"gcc"}

if [ $COMPILER = clang ] ; then
    echo "Using Clang"
    CC=clang
    CXX=clang++
else
    echo "Using GCC"
    CC=gcc
    CXX=g++
fi

CFLAGS="-std=c11 -O1 -Wall -Werror"

# Create a build directory if it does not already exist.
mkdir -p Work

# Remove any artefacts.
rm Work/*.o

# Compile C11 code.
eval $CC $CFLAGS -c Source/util.c           -o Work/util.o
eval $CC $CFLAGS -c Source/config.c         -o Work/config.o
eval $CC $CFLAGS -c Source/gamepad.c        -o Work/gamepad.o
eval $CC $CFLAGS -c Source/cpu/z80.c        -o Work/z80.o
eval $CC $CFLAGS -c Source/sound/sn76489.c  -o Work/sn76489.o
eval $CC $CFLAGS -c Source/video/tms9918a.c -o Work/tms9918a.o
eval $CC $CFLAGS -c Source/video/sms_vdp.c  -o Work/sms_vdp.o
eval $CC $CFLAGS -c Source/sg-1000.c        -o Work/sg-1000.o
eval $CC $CFLAGS -c Source/sms.c            -o Work/sms.o
eval $CC $CFLAGS -c Source/colecovision.c   -o Work/colecovision.o

# C Libraries
eval $CC $CFLAGS -c Libraries/SDL_SavePNG/savepng.c -o Work/SDL_SavePNG.o

# Compile C++11 GUI and link to the rest of the code.
DATE=`date "+%Y-%m-%d"`
eval $CXX \
    Work/*.o \
    Source/main.cpp \
    Source/gui/input.cpp \
    Source/gui/menubar.cpp \
    Source/gui/open.cpp \
    Libraries/imgui-1.76/*.cpp \
    Libraries/imgui-1.76/examples/libs/gl3w/GL/gl3w.c \
    Libraries/imgui-1.76/examples/imgui_impl_opengl3.cpp \
    Libraries/imgui-1.76/examples/imgui_impl_sdl.cpp \
    `sdl2-config --cflags` \
    -I Source/ \
    -I Libraries/imgui-1.76/ \
    -I Libraries/imgui-1.76/examples/libs/gl3w/ \
    `sdl2-config --libs` \
    -framework OpenGL -framework CoreFoundation -ldl -lpng -DBUILD_DATE=\\\"$DATE\\\" \
    -o Snepulator -std=c++11
