param([string]$Toolchain = "$PSScriptRoot\..\..\work\toolchain\llvm-mingw-20261006-ucrt-x86_64", [string]$GuiName = 'ampr-gui.exe')
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force bin | Out-Null
    $include = 'reference/external/ampr_emu/third_party/lz4/lib'
    & "$Toolchain/bin/clang.exe" -O2 -shared -DLZ4_DLL_EXPORT=1 "-I$include" -Ithird_party/lz4 "$include/lz4.c" third_party/lz4/lz4hc.c -o bin/lz4.dll
    if ($LASTEXITCODE) { throw 'LZ4 build failed' }
    & "$Toolchain/bin/clang.exe" -O2 "-I$include" -c "$include/lz4.c" -o bin/lz4.o
    if ($LASTEXITCODE) { throw 'LZ4 object build failed' }
    & "$Toolchain/bin/clang.exe" -O2 "-I$include" -Ithird_party/lz4 -c third_party/lz4/lz4hc.c -o bin/lz4hc.o
    if ($LASTEXITCODE) { throw 'HC object build failed' }
    & "$Toolchain/bin/clang++.exe" -O2 -std=c++17 -municode -static -Isrc -Ireference/external/ampr_emu/include "-I$include" -Ithird_party/lz4 src/main.cpp bin/lz4.o bin/lz4hc.o -lbcrypt -o bin/ampr-native.exe
    if ($LASTEXITCODE) { throw 'Native build failed' }
    & "$Toolchain/bin/clang++.exe" -O2 -std=c++17 -municode -mwindows -static -Isrc -Ireference/external/ampr_emu/include "-I$include" -Ithird_party/lz4 src/gui.cpp bin/lz4.o bin/lz4hc.o -lbcrypt -lcomctl32 -lole32 -luuid -lshell32 "-o" "bin/$GuiName"
    if ($LASTEXITCODE) { throw 'GUI build failed' }
} finally { Pop-Location }
