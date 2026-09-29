# VTPlay 本机启动脚本（开发用）
# MinGW 运行时 DLL 目录：默认 D:\dev\msys64\mingw64\bin，可用 $env:VTPLAY_MINGW_BIN 覆盖
$mingw = if ($env:VTPLAY_MINGW_BIN) { $env:VTPLAY_MINGW_BIN } else { 'D:\dev\msys64\mingw64\bin' }
$exe = if ($env:VTPLAY_EXE) { $env:VTPLAY_EXE } else { Join-Path $PSScriptRoot 'build\src\app\vtplay.exe' }
$env:PATH = "$mingw;$env:PATH"
& $exe @args
