#!/bin/sh

# Exit on first error.
set -e

# Paths
DLL_PATH="/usr/x86_64-w64-mingw32/bin"

# State
TAG=""

# Check we are on a tag
check_tag ()
{
    TAG="$(git describe --exact-match --tags)" || true
    if [ -z "${TAG}" ]
    then
        echo "Not on a tag - aborting."
        exit
    else
        echo "Tag ${TAG} detected."
    fi
}


# Check we've got everything we want to include, and the tools
# needed to generate the release.
check_dependencies ()
{
    ALL_OKAY="true";

    # Tools
    for TOOL in "linuxdeploy-x86_64.AppImage" \
                "x86_64-w64-mingw32-gcc" \
                "zip"
    do
        TOOL_PATH="$(which ${TOOL} 2>/dev/null || true)"
        if [ -z "${TOOL_PATH}" ]
        then
            echo "Missing tool ${TOOL}"
            ALL_OKAY="false"
        fi
    done

    # Files
    for FILE in "${DLL_PATH}/libwinpthread-1.dll" \
                "${DLL_PATH}/SDL2.dll" \
                "${DLL_PATH}/zlib1.dll"
    do
        if [ ! -e "${FILE}" ]
        then
            echo "Missing library: ${FILE}"
            ALL_OKAY="false"
        fi
    done

    if [ ${ALL_OKAY} != "true" ]
    then
        echo "Aborting."
        exit
    fi
}


# Generate Snepulator release (AppImage) for Linux
release_linux ()
{
    echo
    echo "Building for Linux"
    ./build.sh

    (
        cd ${RELEASE_DIR}

        echo "[Desktop Entry]" >> Snepulator.desktop
        echo "Type=Application" >> Snepulator.desktop
        echo "Name=Snepulator" >> Snepulator.desktop
        echo "Exec=Snepulator" >> Snepulator.desktop
        echo "Comment=Snepulator, the emulator with mow!" >> Snepulator.desktop
        echo "Icon=Snepulator_icon_256" >> Snepulator.desktop
        echo "Categories=Emulator;" >> Snepulator.desktop
        echo "Terminal=false" >> Snepulator.desktop

        NO_STRIP=true \
        linuxdeploy-x86_64.AppImage --appdir AppDir -e "../Snepulator" \
            -i "../images/Snepulator_icon_256.png" \
            -d "Snepulator.desktop" \
            --output appimage

        rm -r "AppDir"
        rm "Snepulator.desktop"
        mv "Snepulator-x86_64.AppImage" "Snepulator-${TAG:1}.AppImage"
    )
}


# Generate Snepulator release (.zip) file for Windows
release_windows ()
{
    echo
    echo "Building for Windows"
    ./build.sh windows

    (
        cd ${RELEASE_DIR}

        ZIP_DIR="Snepulator-${TAG:1}"
        mkdir "${ZIP_DIR}"

        cp "../Snepulator.exe" "${ZIP_DIR}"
        cp "${DLL_PATH}/libwinpthread-1.dll" "${ZIP_DIR}"
        cp "${DLL_PATH}/SDL2.dll" "${ZIP_DIR}"
        cp "${DLL_PATH}/zlib1.dll" "${ZIP_DIR}"

        zip -r "Snepulator-${TAG:1}-Windows.zip" "${ZIP_DIR}"
        rm -r "${ZIP_DIR}"
    )
}


check_tag
check_dependencies

RELEASE_DIR="release-${TAG:1}"

if [ -e ${RELEASE_DIR} ]
then
    echo "Release directory '${RELEASE_DIR}' already exists - aborting."
    exit
fi

mkdir $RELEASE_DIR
release_linux
release_windows

echo
echo "Release done."
