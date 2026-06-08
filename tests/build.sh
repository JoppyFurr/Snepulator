#!/bin/sh

# Exit on first error.
set -e

CC=gcc
CFLAGS="-std=c17 -O2 -Wall -Werror"

echo "Building z80-sst..."
$CC $CFLAGS \
    z80-sst.c \
    util.c \
    snepulator_compat.c \
    ../source/cpu/z80.c \
    ../libraries/cJSON-1.7.19/cJSON.c \
    -Werror \
    -o z80-sst

echo "Building m68k-sst..."
$CC $CFLAGS \
    m68k-sst.c \
    util.c \
    snepulator_compat.c \
    ../source/cpu/m68k.c \
    ../libraries/cJSON-1.7.19/cJSON.c \
    -Werror \
    -o m68k-sst

echo "Done."
