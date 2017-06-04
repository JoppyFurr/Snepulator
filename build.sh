#!/bin/sh

# Create a build directory if it does not already exist.
mkdir -p Work

# Remove any artefacts.
rm Work/*.o

# Compile C11 code.
gcc -c Source/cpu/z80.c         -o Work/z80.o       -std=c11 -Wall -Werror
gcc -c Source/gpu/sega_vdp.c    -o Work/sega_vdp.o  -std=c11 -Wall -Werror

# Compile C++11 GUI and link to the rest of the code.
g++ Source/main.cpp Work/*.o \
    Libraries/imgui-1.49/*.cpp \
    Libraries/imgui-1.49/examples/libs/gl3w/GL/gl3w.c \
    Libraries/imgui-1.49/examples/sdl_opengl3_example/imgui_impl_sdl_gl3.cpp \
    `sdl2-config --cflags` \
    -I Libraries/imgui-1.49/ \
    -I Libraries/imgui-1.49/examples/libs/gl3w/ \
    `sdl2-config --libs` \
    -lGL -ldl -DBUILD_DATE=\"`date --rfc-3339=date`\" \
    -o Snepulator -std=c++11
