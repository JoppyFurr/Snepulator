#!/bin/sh

# Exit on first error.
set -e


# Defaults
CC=gcc
CXX=g++
SDL2_CONFIG="sdl2-config"
IMGUI_VERSION="imgui-1.86"
CLEAN_BUILD="false"

EXTRA_FLAGS=""
EXTRA_LINKS=""
IMGUI_PATH="libraries/${IMGUI_VERSION}"

# Remove old artefacts
build_prepare ()
{
    # Create a build directory if it does not already exist.
    mkdir -p work

    # If there is no build_id.txt, or it is different, then do a clean build.
    if [ ! -e "build_id.txt" ]
    then
        CLEAN_BUILD="true"
    elif [ "${BUILD_ID}" != "$(cat build_id.txt)" ]
    then
        CLEAN_BUILD="true"
    fi

    if [ "${CLEAN_BUILD}" = "true" ]
    then
        # Re-build Snepulator and ImGui
        rm -rf work/imgui
        rm -f work/*.o
    else
        # Only re-build Snepulator
        rm -f work/*.o
    fi
}

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
    eval $CXX $CXXFLAGS -c source/main.cpp        -o work/main.o
    eval $CXX $CXXFLAGS -c source/shader.cpp      -o work/shader.o
    eval $CXX $CXXFLAGS -c source/gui/input.cpp   -o work/input.o
    eval $CXX $CXXFLAGS -c source/gui/menubar.cpp -o work/menubar.o
    eval $CXX $CXXFLAGS -c source/gui/open.cpp    -o work/open.o
}


# Campile ImGui if we haven't already.
build_imgui ()
{
    mkdir -p work/imgui

    # Early return if ImGui has already been built.
    if [ -e work/imgui/imgui.o -a ${IMGUI_PATH}/imgui.cpp -ot work/imgui/imgui.o ]
    then
        return
    fi

    echo "Compiling ImGui..."
    eval $CXX $CXXFLAGS -c ${IMGUI_PATH}/imgui.cpp                       -o work/imgui/imgui.o
    eval $CXX $CXXFLAGS -c ${IMGUI_PATH}/imgui_demo.cpp                  -o work/imgui/imgui_demo.o
    eval $CXX $CXXFLAGS -c ${IMGUI_PATH}/imgui_draw.cpp                  -o work/imgui/imgui_draw.o
    eval $CXX $CXXFLAGS -c ${IMGUI_PATH}/imgui_tables.cpp                -o work/imgui/imgui_tables.o
    eval $CXX $CXXFLAGS -c ${IMGUI_PATH}/imgui_widgets.cpp               -o work/imgui/imgui_widgets.o
    eval $CXX $CXXFLAGS -c ${IMGUI_PATH}/backends/imgui_impl_opengl3.cpp -o work/imgui/imgui_impl_opengl3.o
    eval $CXX $CXXFLAGS -c ${IMGUI_PATH}/backends/imgui_impl_sdl.cpp     -o work/imgui/imgui_impl_sdl.o
}


# Compile other libraries.
build_libraries ()
{
    echo "Compiling libraries..."
    eval $CC $CFLAGS -c libraries/BLAKE3/blake3.c          -o work/blake3.o
    eval $CC $CFLAGS -c libraries/BLAKE3/blake3_portable.c -o work/blake3_portable.o
    eval $CC $CFLAGS -DBLAKE3_NO_SSE2 -DBLAKE3_NO_SSE41 -DBLAKE3_NO_AVX2 -DBLAKE3_NO_AVX512 \
                     -c libraries/BLAKE3/blake3_dispatch.c -o work/blake3_dispatch.o
    eval $CC $CFLAGS -c libraries/gl3w/GL/gl3w.c           -o work/gl3w.o
    eval $CC $CFLAGS -c libraries/libspng-0.7.4/spng.c     -o work/spng.o
}


# Build-host options
if [ $(uname) = "Darwin" ]
then
    # MacOS
    DATE=$(date "+%Y-%m-%d")
    EXTRA_LINKS="-framework OpenGL -framework CoreFoundation"
    EXTRA_FLAGS="-DTARGET_DARWIN"
else
    # Linux
    DATE=$(date --rfc-3339=date)
    EXTRA_LINKS="$(pkg-config --libs gl)"
fi


# Parameters
while [ ${#} -gt 0 ]
do
    case ${1} in
        clean)
            echo "Clean build"
            CLEAN_BUILD="true"
            ;;
        clang)
            echo "Using Clang"
            CC="clang"
            CXX="clang++"
            ;;
        windows)
            echo "Windows build (x86_64)"
            CC=x86_64-w64-mingw32-gcc
            CXX=x86_64-w64-mingw32-g++
            SDL2_CONFIG="/usr/x86_64-w64-mingw32/bin/sdl2-config"
            EXTRA_FLAGS="${EXTRA_FLAGS} -DSPNG_STATIC -DTARGET_WINDOWS"
            EXTRA_LINKS="-lopengl32 -static-libgcc -static-libstdc++"
            ;;
        dev*)
            echo "Developer options enabled"
            EXTRA_FLAGS="${EXTRA_FLAGS} -DDEVELOPER_BUILD"
            ;;
        *)
            echo "Unknown option ${1}, aborting."
            exit
            ;;
    esac

    shift
done

CFLAGS="-std=c11 -O2 -Wall -Werror -D_POSIX_C_SOURCE=200809L \
        $(${SDL2_CONFIG} --cflags) \
        -I libraries/BLAKE3/ \
        -I libraries/gl3w/ \
        -I libraries/libspng-0.7.4/ \
        -DBUILD_DATE=\\\"$DATE\\\" \
        ${EXTRA_FLAGS}"

CXXFLAGS="-std=c++11 -O2 -Wall -Werror \
          $(${SDL2_CONFIG} --cflags) \
          -I libraries/gl3w/ \
          -I ${IMGUI_PATH}/ \
          -I ${IMGUI_PATH}/backends/ \
          -DBUILD_DATE=\\\"$DATE\\\" \
          ${EXTRA_FLAGS}"

# Keep track of the compiler name and imgui version.
# If they change, we can force a clean build.
BUILD_ID="${CC},${IMGUI_VERSION}"
build_prepare
echo ${BUILD_ID} > build_id.txt

# Compile the various components.
build_snepulator
build_snepulator_gui
build_imgui
build_libraries

# For Windows, bake in the icon
if [ "${CC}" == "x86_64-w64-mingw32-gcc" ]
then
    echo 'IDI_APPLICATION ICON "images/snepulator_icon.ico"' > "work/icon.rc"
    x86_64-w64-mingw32-windres "work/icon.rc" -O coff -o "work/icon.o"
fi


# Create executable.
echo "Linking..."
eval $CXX $CXXFLAGS \
    work/*.o \
    work/imgui/*.o \
    $(${SDL2_CONFIG} --libs) \
    -lz -lm -lpthread \
    ${EXTRA_LINKS} \
    -o Snepulator

echo "Done."
