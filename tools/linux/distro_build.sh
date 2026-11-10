#!/bin/sh
# Build the app (start screen only, no game code) natively in each distribution's container. What differs between
# distributions is exactly what a container reproduces: package names, FFmpeg and shaderc libraries
# (libshaderc.so vs libshaderc_shared.so), lld, and the clang version
# (the lifted FP code needs __builtin_elementwise_*). A virtual machine per distribution would test the same thing
# an order of magnitude slower; QEMU matters for other CPU architectures (the aarch64 AppImage), not for this.
#
# Usage: tools/linux/distro_build.sh [image...]     (default: every distribution in DISTROS)
#        ENGINE=docker|podman JOBS=8 tools/linux/distro_build.sh archlinux:latest
#        PKG_CACHE=0 tools/linux/distro_build.sh     (download every package again)
# The source tree is mounted read-only and objects go to /tmp/build inside the container; GEN points nowhere, so a
# checkout with generated/ still builds only the app. Downloaded packages (and
# the package metadata where the package manager keeps it there) stay in one engine volume per distribution,
# swr-pkgcache-<image>, so a second run doesn't download them again; `$ENGINE volume rm` drops one.
set -e
REPO=$(cd "$(dirname "$0")/../.." && pwd)
ENGINE=${ENGINE:-$(command -v podman || command -v docker)}
JOBS=${JOBS:-$(nproc)}
PLATFORM=--platform=linux/amd64

# Keep this list aligned with the distributions tested by the Linux CI workflow.
DISTROS="debian:13 ubuntu:26.04 fedora:44 archlinux:latest opensuse/tumbleweed:latest"

deps_for() {
    case $1 in
        debian*|ubuntu*)  # clang 19+ / 20+, libshaderc.so; the image's docker-clean deletes the .debs
            echo 'rm -f /etc/apt/apt.conf.d/docker-clean; apt-get update -qq; \
                apt-get install -y -qq --no-install-recommends make clang lld pkg-config \
                ca-certificates curl cmake ninja-build libusb-1.0-0-dev \
                libavformat-dev libavcodec-dev libavutil-dev libswscale-dev \
                libvulkan-dev libshaderc-dev zlib1g-dev' ;;
        fedora*)          # libshaderc-devel, FFmpeg as the "free" variant
            # Fedora's stub provides the link-time ABI without downloading from Cisco.
            echo 'dnf install -y -q --disable-repo=fedora-cisco-openh264 --setopt=keepcache=True \
                make clang lld pkgconf-pkg-config libshaderc-devel vulkan-loader-devel zlib-devel \
                ca-certificates curl-minimal tar gzip cmake ninja-build libusb1-devel \
                ffmpeg-free-devel libavformat-free-devel libavcodec-free-devel libavutil-free-devel \
                libswscale-free-devel noopenh264' ;;
        arch*)
            echo 'pacman -Syu --noconfirm --quiet --needed \
                base-devel clang lld shaderc vulkan-icd-loader vulkan-headers ffmpeg zlib \
                ca-certificates curl cmake ninja libusb' ;;
        opensuse*)        # Tumbleweed: FFmpeg headers are in /usr/include/ffmpeg
            echo 'zypper -q modifyrepo --all --keep-packages; \
                zypper -q --non-interactive --gpg-auto-import-keys refresh; \
                zypper -q --non-interactive install --no-recommends \
                make clang lld gcc-c++ pkgconf-pkg-config shaderc-devel vulkan-devel zlib-devel \
                ca-certificates curl tar gzip cmake ninja libusb-1_0-devel \
                libavformat-devel libavcodec-devel libavutil-devel libswscale-devel' ;;
        *) return 2 ;;
    esac
}

cache_dir_for() {  # where the image's package manager keeps downloaded packages
    case $1 in
        debian*|ubuntu*) echo /var/cache/apt/archives ;;
        fedora*)         echo /var/cache/libdnf5 ;;
        arch*)           echo /var/cache/pacman/pkg ;;
        opensuse*)       echo /var/cache/zypp ;;
    esac
}

images=${*:-$DISTROS}
failed=
for image in $images; do
    if ! install=$(deps_for "$image"); then
        echo "distro_build.sh: no dependency list for $image" >&2
        failed="$failed $image"
        continue
    fi
    echo "=== $image"
    log=$(mktemp)
    cache=
    volume=swr-pkgcache-$(echo "$image" | tr -c 'a-zA-Z0-9_.\n-' _)
    [ "${PKG_CACHE:-1}" = 0 ] || cache="-v $volume:$(cache_dir_for "$image")"
    if "$ENGINE" run --rm $PLATFORM -v "$REPO":/src:ro $cache -w /src \
            -e SW_INSTALL="$install" -e JOBS="$JOBS" "$image" \
            sh -ec 'eval "$SW_INSTALL"
                    sh tools/build_ci_sdl.sh /tmp/ci-sdl -DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_X11=OFF -DSDL_WAYLAND=OFF
                    sdl=/tmp/ci-sdl/install
                    make -j"$JOBS" B=/tmp/build GEN=/nonexistent SDL3_ROOT="$sdl"
                    LD_LIBRARY_PATH="$sdl/lib" ldd /tmp/build/savagewind > /tmp/savagewind-libraries
                    grep -F "=> $sdl/lib/libSDL3.so" /tmp/savagewind-libraries
                    grep -E "avformat|shaderc" /tmp/savagewind-libraries' \
            > "$log" 2>&1 < /dev/null; then
        echo "=== $image OK"
        grep -E "SDL3|avformat|shaderc" "$log" | sed "s/^/    /"
    else
        echo "=== $image FAILED (full log: $log)"
        tail -40 "$log" | sed "s/^/    /"
        failed="$failed $image"
        continue
    fi
    rm -f "$log"
done
if [ -n "$failed" ]; then echo "failed:$failed" >&2; exit 1; fi
echo "all distributions built"
