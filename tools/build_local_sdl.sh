#!/bin/sh
# Build the adjacent SDL fork with the same toolchain as the Windows runtime.
set -eu

source_dir=$1
work=$2
shift 2

usb_dll=$(command -v libusb-1.0.dll || true)
if [ -z "$usb_dll" ]; then
    for usb in "$source_dir"/build/deps/libusb-*/; do
        [ -f "$usb/include/libusb.h" ] || continue
        [ -f "$usb/MinGW64/static/libusb-1.0.dll.a" ] || continue
        [ -f "$usb/MinGW64/dll/libusb-1.0.dll" ] || continue
        usb=$(cd "$usb" && pwd)
        set -- "$@" "-DLibUSB_INCLUDE_PATH=$(cygpath -m "$usb/include")" \
                     "-DLibUSB_LIBRARY=$(cygpath -m "$usb/MinGW64/static/libusb-1.0.dll.a")"
        usb_dll="$usb/MinGW64/dll/libusb-1.0.dll"
        break
    done
fi

cmake -S "$source_dir" -B "$work/obj" -G Ninja \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_LIBDIR=lib \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF \
    -DSDL_EXAMPLES=OFF -DSDL_HIDAPI_LIBUSB=ON -DSDL_HIDAPI_LIBUSB_SHARED=ON "$@"
if ! grep -q '^HAVE_LIBUSB_H:INTERNAL=1$' "$work/obj/CMakeCache.txt" || [ -z "$usb_dll" ]; then
    echo "Install the UCRT64 libusb package or provide the x64 libusb SDK in $source_dir/build/deps." >&2
    exit 1
fi
cmake --build "$work/obj" --parallel "${SDL3_JOBS:-8}"
cmake --install "$work/obj" --prefix "$work/install"
installed_usb="$work/install/bin/libusb-1.0.dll"
if [ ! -f "$installed_usb" ] || [ "$(sha256sum < "$usb_dll")" != "$(sha256sum < "$installed_usb")" ]; then
    cp "$usb_dll" "$installed_usb"
fi
