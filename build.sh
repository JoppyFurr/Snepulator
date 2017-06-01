#!/bin/sh

gcc Source/main.c Source/gpu/sega_vdp.c Source/cpu/z80.c -o Snepulator -lSDL2 -DBUILD_DATE=\"`date --rfc-3339=date`\" -std=c11 -Wall -Werror
