#!/bin/sh

# Exit on first error.
set -e

CC=gcc
CFLAGS="-std=c17 -O2 -Wall -Werror"

# Caching
if command -v ccache > /dev/null
then
    echo "Using ccache"
    CC="ccache $CC"
    CXX="ccache $CXX"
fi

# Create a build directory if it does not already exist.
mkdir -p work

# Compile all objects.
echo "Compiling... "
eval $CC $CFLAGS -c ../libraries/cJSON-1.7.19/cJSON.c   -o work/cJSON.o
eval $CC $CFLAGS -c ../source/cpu/m68k.c                -o work/m68k.o
eval $CC $CFLAGS -c ../source/cpu/z80.c                 -o work/z80.o
eval $CC $CFLAGS -c ./snepulator_compat.c               -o work/snepulator_compat.o
eval $CC $CFLAGS -c ./util.c                            -o work/util.o
eval $CC $CFLAGS -c ./z80-sst.c                         -o work/z80-sst.o
eval $CC $CFLAGS -c ./m68k-sst.c                        -o work/m68k-sst.o

# Link the binaries
echo "Linking..."

$CC $CFLAGS work/z80-sst.o \
            work/util.o \
            work/snepulator_compat.o \
            work/z80.o \
            work/cJSON.o \
            -Werror \
            -o z80-sst

$CC $CFLAGS work/m68k-sst.o \
            work/util.o \
            work/snepulator_compat.o \
            work/m68k.o \
            work/cJSON.o \
            -lpthread \
            -Werror \
            -o m68k-sst

echo "Done."
