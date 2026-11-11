#!/bin/sh
# Build the pinned SDL fork for development, CI or packaging.
set -eu

work=$1
shift
jobs=${JOBS:-$(nproc)}
revision=cb98e17ffd7443a741c2c45b398df2f56ddb33b4

mkdir -p "$work"
work=$(cd "$work" && pwd)
src="$work/src-$revision"
obj="$work/obj-$revision"
if [ ! -f "$src/.unpacked" ]; then
    mkdir -p "$src"
    curl --fail --silent --show-error --location --retry 3 \
        "https://codeload.github.com/qiyuey/SDL/tar.gz/$revision" -o "$work/sdl.tar.gz"
    tar -xzf "$work/sdl.tar.gz" --strip-components=1 -C "$src"
    touch "$src/.unpacked"
fi
cmake -S "$src" -B "$obj" -G Ninja -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="${SDL_INSTALL_PREFIX:-$work/install}" -DCMAKE_INSTALL_LIBDIR=lib \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF \
    -DSDL_HIDAPI_LIBUSB=ON -DSDL_HIDAPI_LIBUSB_SHARED=ON "$@"
if ! grep -q '^HAVE_LIBUSB_H:INTERNAL=1$' "$obj/CMakeCache.txt"; then
    echo 'The custom SDL3 build requires libusb headers and libraries.' >&2
    exit 1
fi
cmake --build "$obj" --parallel "$jobs"
cmake --install "$obj"
