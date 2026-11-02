#!/bin/sh
# Run from an MSYS2 UCRT64 shell. All scratch files stay under build/.
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
clang -O2 -Wall -Iruntime tools/test_log.c -o build/test_log.exe
./build/test_log.exe
clang -O2 -Wall -Iruntime tools/test_texdecode.c runtime/nvn/astc.c runtime/nvn/vulkan/texdecode.c \
    -o build/test_texdecode.exe -lSDL3 -lpthread
./build/test_texdecode.exe
SAVAGEWIND_ASTC_WORKERS=0 ./build/test_texdecode.exe
clang -O2 -Wall -Iruntime tools/test_tegra_copy.c runtime/nvn/tegra.c -o build/test_tegra_copy.exe
./build/test_tegra_copy.exe
clang -O2 -Wall -Iruntime tools/test_settings_language.c runtime/settings.c runtime/language.c \
    -o build/test_settings_language.exe
for spec in 'missing zh-Hans' 'legacy zh-Hans' 'auto auto' 'EUfr fr' 'TWzh zh-Hant'; do
    SAVAGEWIND_SETTINGS=build/test-language.ini ./build/test_settings_language.exe $spec
done
