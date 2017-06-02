#!/bin/sh

# Create a build directory if it does not already exist.
mkdir -p Work

# Remove any artefacts.
rm Work/*.o

# Compile C11 code.
gcc -c Source/cpu/z80.c         -o Work/z80.o       -std=c11 -Wall -Werror
gcc -c Source/gpu/sega_vdp.c    -o Work/sega_vdp.o  -std=c11 -Wall -Werror

# Compile C++11 GUI and link.
g++ Source/main.cpp Work/*.o    -o Snepulator       -lSDL2 -DBUILD_DATE=\"`date --rfc-3339=date`\" -std=c++11 -Wall -Werror
