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
mkdir -p work

# Remove any artefacts.
rm work/*.o

# Compile C11 code.
echo "Compiling emulator..."
eval $CC $CFLAGS -c source/cpu/z80.c         -o work/z80.o
eval $CC $CFLAGS -c source/database/sms_db.c -o work/sms_db.o
eval $CC $CFLAGS -c source/sound/sn76489.c   -o work/sn76489.o
eval $CC $CFLAGS -c source/video/filter.c    -o work/filter.o
eval $CC $CFLAGS -c source/video/tms9928a.c  -o work/tms9928a.o
eval $CC $CFLAGS -c source/video/sms_vdp.c   -o work/sms_vdp.o
eval $CC $CFLAGS -c source/colecovision.c    -o work/colecovision.o
eval $CC $CFLAGS -c source/config.c          -o work/config.o
eval $CC $CFLAGS -c source/gamepad.c         -o work/gamepad.o
eval $CC $CFLAGS -c source/sg-1000.c         -o work/sg-1000.o
eval $CC $CFLAGS -c source/save_state.c      -o work/save_state.o
eval $CC $CFLAGS -c source/sms.c             -o work/sms.o
eval $CC $CFLAGS -c source/util.c            -o work/util.o

# C Libraries
echo "Compiling libraries..."
eval $CC $CFLAGS -c libraries/BLAKE3/blake3.c -o work/blake3.o
eval $CC $CFLAGS -c libraries/BLAKE3/blake3_portable.c -o work/blake3_portable.o
eval $CC $CFLAGS -c libraries/BLAKE3/blake3_dispatch.c -o work/blake3_dispatch.o \
                 -DBLAKE3_NO_SSE2 -DBLAKE3_NO_SSE41 -DBLAKE3_NO_AVX2 -DBLAKE3_NO_AVX512
eval $CC $CFLAGS -c libraries/SDL_SavePNG/savepng.c -o work/SDL_SavePNG.o

# OS-specific compiler options
if [ $(uname) = "Darwin" ]
then
    # MacOS
    DATE=$(date "+%Y-%m-%d")
    OSFLAGS="-framework OpenGL -framework CoreFoundation"
else
    # Linux
    DATE=$(date --rfc-3339=date)
    OSFLAGS="$(pkg-config --libs gl)"
fi

# Compile C++11 GUI and link to the rest of the code.
echo "Compiling GUI and linking..."
eval $CXX \
    work/*.o \
    source/main.cpp \
    source/gui/input.cpp \
    source/gui/menubar.cpp \
    source/gui/open.cpp \
    -DIMGUI_IMPL_OPENGL_LOADER_GL3W \
    libraries/imgui-1.76/*.cpp \
    libraries/imgui-1.76/examples/libs/gl3w/GL/gl3w.c \
    libraries/imgui-1.76/examples/imgui_impl_opengl3.cpp \
    libraries/imgui-1.76/examples/imgui_impl_sdl.cpp \
    `sdl2-config --cflags` \
    -I source/ \
    -I libraries/imgui-1.76/ \
    -I libraries/imgui-1.76/examples/libs/gl3w/ \
    `sdl2-config --libs` \
    -ldl -lpng -lm -DBUILD_DATE=\\\"$DATE\\\" \
    -lpthread \
    $OSFLAGS \
    -o Snepulator -std=c++11

echo "Done."
