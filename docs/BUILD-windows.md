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
    make gen        # recompile from your own game files in extracted/exefs
    make -j8        # build/savagewind.exe and build/savagewind-config.exe

The runtime also uses PGO when `build/local-runtime.profdata` exists.
To use another profile, pass `PGO=use PGO_PROFILE=<path-to-merged-profdata>`.

NVIDIA DLSS needs no SDK at build time: `runtime/nvn/vulkan/dlss.c` loads the NVIDIA driver's NGX at run time, and
NGX loads `nvngx_dlss.dll` and `nvngx_dlssg.dll` from the folder of `savagewind.exe`. Neither is in this repository or
in `make dist`: the setup downloads NVIDIA's own copies (`runtime/dlss_fetch.c`, NVIDIA/DLSS `v310.9.16`, size and
SHA-256 checked). For a source build, run the setup once, or copy the two DLLs of that release into `build/`.

When `../SDL3` exists, Make incrementally builds its current source as a libusb-enabled UCRT64 Release
dependency and copies SDL3 and libusb into the game's `build/`. Sync that checkout separately to control
the version used by the next build. Set `SDL3_JOBS` to adjust SDL's build parallelism.

Standalone checkouts and CI can still build the pinned fork with `sh tools/build_ci_sdl.sh build/fork-sdl`.
Then pass `SDL3_ROOT=build/fork-sdl/install SDL3_LIBDIR=build/fork-sdl/install/bin
SDL3_LIB=build/fork-sdl/install/lib/libSDL3.dll.a` to Make. An explicit `SDL3_ROOT` selects prebuilt SDL
and bypasses the local source build.

Windows builds default to `RELEASE=1`. The executable opens without a console and writes to
`savagewind.log` when started from Explorer. Use `make -j RELEASE=0` for a console build.

Place a local icon at `build/botw-local.ico` to embed it in both Windows executables on each rebuild.
Use `WINDOWS_ICON=path/to/icon.ico` to select another icon. Without a local icon, Make uses the
repository icon at `docs/icon/icon.ico`.

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
