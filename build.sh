#!/bin/sh

gcc Source/main.c -o Snepulator -lSDL2 -DBUILD_DATE=\"`date --rfc-3339=date`\" -std=c99
