![Snepulator](images/snepulator_banner.png)

# Snepulator/tests

This directory currently contains the test-harness for running the Single-Step Tests against
Snepulator's CPU implementations.

The test binaries can be built by running `./build.sh`

## z80-sst

You will first need to checkout a copy of the m68k tests: `https://github.com/SingleStepTests/z80`
This should end up checked out to `Snepulator/tests/z80`

Note that currently the undocumented flags ('X' and 'Y') are masked out by default. Checking their
behaviour can be enabled by editing an `#if 0` in `z80-sst.c` and re-building.

Tests can be run with `./z80-sst`


## m68k-sst

You will first need to checkout a copy of the m68000 tests: `https://github.com/SingleStepTests/m68000`
This should end up checked out to `Snepulator/tests/m68000`

Before running the first time, the tests need to be decoded with `m68000/decode.py`

Note that not all m68k instructions have been implemented yet. To allow the test program to check if
instructions are implemented before running them, the `m68k_instruction` array in `source/cpu/m68k.c`
needs to be made non-static

Tests can be run with `./z80-sst`
