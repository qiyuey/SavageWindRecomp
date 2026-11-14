# Building Savage Wind on Windows (x86-64)

The personal fork builds a local Vulkan executable from your own game files.

## One-time setup
Run in PowerShell from the repo root:

    powershell -ExecutionPolicy Bypass -File tools\setup_windows.ps1

This installs MSYS2 (UCRT64: clang, lld, make, SDL3, FFmpeg, Vulkan loader + headers, shaderc, Vulkan Memory
Allocator, validation layers), Python 3.12 for the tools (`tools\venv`: unicorn, capstone, lz4) and switches the
clone to LF line endings.

## Build
The Windows build statically links the generated game code into `savagewind.exe`. Both the runtime and game code
use `-O2 -march=znver4 -flto=thin` by default.

From an MSYS2 UCRT64 shell, or Git Bash with MSYS2 first on PATH:

    export PATH="/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH"
    sh tools/build_ci_sdl.sh build/fork-sdl
    export SDL3_ROOT=build/fork-sdl/install
    export SDL3_LIBDIR="$SDL3_ROOT/bin"
    export SDL3_LIB="$SDL3_ROOT/lib/libSDL3.dll.a"
    make gen        # recompile from your own game files in extracted/exefs
    make -j8        # build/savagewind.exe and build/savagewind-config.exe

The runtime also uses PGO when `build/local-runtime.profdata` exists.
To use another profile, pass `PGO=use PGO_PROFILE=<path-to-merged-profdata>`.

The SDL build helper needs curl and the UCRT64 packages for CMake, Ninja and libusb. It builds the
pinned fork with frequency rumble. You can also use a sibling `SDL3` checkout at the same revision
with a libusb-enabled x64 Release build in `build/windows-x64-release/Release`. In that case omit
the SDL build helper and the three exports above. Make copies the selected SDL3 DLL if the build directory needs one.

Windows builds default to `RELEASE=1`. The executable opens without a console and writes to
`savagewind.log` when started from Explorer. Use `make -j RELEASE=0` for a console build.

Run `make gen` again after updating the generator or changing the game files. The configuration screen manages
game assets and settings. Use `make runtime-objs` to compile the runtime without generated game code.

The Windows executable contains locally recompiled game code. Keep it in `build/` alongside the external runtime
DLLs, extracted game files, saves and settings. Obtain updates by rebuilding the fork locally.

Keep the two DLC RomFS directories at `build/extracted/aoc1/` and `build/extracted/aoc2/` when
running from `build/`. The executable uses the base game's recompiled code to mount both DLC
directories at runtime. See [DLC layout](game-files.md#the-dlc).

To enable the local Git check for ignored game content and staged blobs over 50 MiB, run
`git config core.hooksPath .githooks` once in this checkout.

## Differences from macOS
- Guest window 0x70_0000_0000..0x73_2000_0000 is reserved with VirtualAlloc and committed per region
  (kernel regions, modules, HLE heap); stray first touches are committed by a vectored exception handler.
- AArch64 FP/SIMD instructions are lowered to C (`gen.py --simd=c`, the default on non-arm64 hosts) instead of
  inline-asm passthrough.
- Renderer: Vulkan (runtime/nvn/vulkan) instead of Metal; shaders are translated to GLSL and compiled with
  shaderc. macOS keeps Metal.
- "Quit to Start Screen" reboots the game in place on both (runtime/main.c); the only re-exec left is
  the startup retry when the guest window is taken, which on Windows starts a new process and exits.

## Tests
- `tools/test_translate.sh`: shader translator fuzz (every GLSL translation compiles and validates; with `REF=<commit>`,
  also the MSL output compared with that commit's translator). Exits 1 on any failure.
- `tools/test_vk_pipelines.sh`: fuzzed shaders through the backend's compile/reflection into real pipelines.
- `SAVAGEWIND_VK_VALIDATION=1` with `VK_ADD_LAYER_PATH` pointing at `C:\msys64\ucrt64\bin`: Khronos validation layer.
- Run test windows directly, not under MSYS `timeout` (it stalls the window).
