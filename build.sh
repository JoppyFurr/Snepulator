#!/bin/sh

# Exit on first error.
set -e

# Compile Snepulator emulator core.
build_snepulator ()
{
    echo "Compiling emulator..."
    eval $CC $CFLAGS -c source/cpu/z80.c            -o work/z80.o
    eval $CC $CFLAGS -c source/database/sg_db.c     -o work/sg_db.o
    eval $CC $CFLAGS -c source/database/sms_db.c    -o work/sms_db.o
    eval $CC $CFLAGS -c source/sound/band_limit.c   -o work/band_limit.o
    eval $CC $CFLAGS -c source/sound/sn76489.c      -o work/sn76489.o
    eval $CC $CFLAGS -c source/sound/ym2413.c       -o work/ym2413.o
    eval $CC $CFLAGS -c source/video/tms9928a.c     -o work/tms9928a.o
    eval $CC $CFLAGS -c source/video/sms_vdp.c      -o work/sms_vdp.o
    eval $CC $CFLAGS -c source/colecovision.c       -o work/colecovision.o
    eval $CC $CFLAGS -c source/config.c             -o work/config.o
    eval $CC $CFLAGS -c source/gamepad.c            -o work/gamepad.o
    eval $CC $CFLAGS -c source/gamepad_sdl.c        -o work/gamepad_sdl.o
    eval $CC $CFLAGS -c source/logo.c               -o work/logo.o
    eval $CC $CFLAGS -c source/path.c               -o work/path.o
    eval $CC $CFLAGS -c source/sg-1000.c            -o work/sg-1000.o
    eval $CC $CFLAGS -c source/save_state.c         -o work/save_state.o
    eval $CC $CFLAGS -c source/sms.c                -o work/sms.o
    eval $CC $CFLAGS -c source/snepulator.c         -o work/snepulator.o
    eval $CC $CFLAGS -c source/util.c               -o work/util.o
    eval $CC $CFLAGS -c source/vgm_player.c         -o work/vgm_player.o
}


# Compile Snepulator GUI.
build_snepulator_gui ()
{
    echo "Compiling GUI..."
    eval $CXX $CXXFLAGS $GUIFLAGS -c source/main.cpp        -o work/main.o
    eval $CXX $CXXFLAGS $GUIFLAGS -c source/shader.cpp      -o work/shader.o
    eval $CXX $CXXFLAGS $GUIFLAGS -c source/gui/input.cpp   -o work/input.o
    eval $CXX $CXXFLAGS $GUIFLAGS -c source/gui/menubar.cpp -o work/menubar.o
    eval $CXX $CXXFLAGS $GUIFLAGS -c source/gui/open.cpp    -o work/open.o
}


# Campile ImGui if we haven't already.
build_imgui ()
{
    mkdir -p work/imgui

    # Early return if ImGui hasn't been changed.
    if [ -e work/imgui/imgui.o -a libraries/imgui-1.86/imgui.cpp -ot work/imgui/imgui.o ]
    then
        return
    fi

    echo "Compiling ImGui..."
    eval $CXX $CXXFLAGS $GUIFLAGS -c libraries/imgui-1.86/imgui.cpp                       -o work/imgui/imgui.o
    eval $CXX $CXXFLAGS $GUIFLAGS -c libraries/imgui-1.86/imgui_demo.cpp                  -o work/imgui/imgui_demo.o
    eval $CXX $CXXFLAGS $GUIFLAGS -c libraries/imgui-1.86/imgui_draw.cpp                  -o work/imgui/imgui_draw.o
    eval $CXX $CXXFLAGS $GUIFLAGS -c libraries/imgui-1.86/imgui_tables.cpp                -o work/imgui/imgui_tables.o
    eval $CXX $CXXFLAGS $GUIFLAGS -c libraries/imgui-1.86/imgui_widgets.cpp               -o work/imgui/imgui_widgets.o
    eval $CXX $CXXFLAGS $GUIFLAGS -c libraries/imgui-1.86/backends/imgui_impl_opengl3.cpp -o work/imgui/imgui_impl_opengl3.o
    eval $CXX $CXXFLAGS $GUIFLAGS -c libraries/imgui-1.86/backends/imgui_impl_sdl.cpp     -o work/imgui/imgui_impl_sdl.o
}


# Compile other libraries.
build_libraries ()
{
    echo "Compiling libraries..."
    eval $CC $CFLAGS -c libraries/BLAKE3/blake3.c          -o work/blake3.o
    eval $CC $CFLAGS -c libraries/BLAKE3/blake3_portable.c -o work/blake3_portable.o
    eval $CC $CFLAGS -DBLAKE3_NO_SSE2 -DBLAKE3_NO_SSE41 -DBLAKE3_NO_AVX2 -DBLAKE3_NO_AVX512 \
                     -c libraries/BLAKE3/blake3_dispatch.c -o work/blake3_dispatch.o
    eval $CC $CFLAGS -c libraries/SDL_SavePNG/savepng.c    -o work/SDL_SavePNG.o
}


# Defaults
CC=gcc
CXX=g++


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

CFLAGS="-std=c11 -O2 -Wall -Werror -D_POSIX_C_SOURCE=200809L"
CXXFLAGS="-std=c++11 -O2 -Wall -Werror"
GUIFLAGS="$(sdl2-config --cflags) \
          -DBUILD_DATE=\\\"$DATE\\\" \
          -I libraries/imgui-1.86/ \
          -I libraries/imgui-1.86/backends/"

# Parameters
while [ ${#} -gt 0 ]
do
    case ${1} in
        clean)
            echo "Clean build"
            rm -r work/imgui
            ;;
        clang)
            echo "Using Clang"
            CC=clang
            CXX=clang++
            ;;
        dev*)
            echo "Developer options enabled"
            CFLAGS="${CFLAGS} -DDEVELOPER_BUILD"
            CXXFLAGS="${CXXFLAGS} -DDEVELOPER_BUILD"
            ;;
        *)
            echo "Unknown option ${1}, aborting."
            exit
            ;;
    esac

    shift
done

# Create a build directory if it does not already exist.
mkdir -p work

# Remove previous artefacts.
# Ignore return code, as the files may not exist.
rm -f work/*.o

# Compile the various components.
build_snepulator
build_snepulator_gui
build_imgui
build_libraries

# Create executible.
echo "Linking..."
eval $CXX $CXXFLAGS $GUIFLAGS \
    work/*.o \
    work/imgui/*.o \
    $OSFLAGS \
    $(sdl2-config --libs) \
    -ldl -lpng -lz -lm -lpthread \
    -o Snepulator

echo "Done."
