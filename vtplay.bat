@echo off
REM VTPlay 本机启动脚本（开发用）
REM MinGW 运行时 DLL 目录：默认 D:\dev\msys64\mingw64\bin，可用 VTPLAY_MINGW_BIN 覆盖
setlocal
if "%VTPLAY_MINGW_BIN%"=="" set "VTPLAY_MINGW_BIN=D:\dev\msys64\mingw64\bin"
if "%VTPLAY_EXE%"=="" set "VTPLAY_EXE=%~dp0build\src\app\vtplay.exe"
set "PATH=%VTPLAY_MINGW_BIN%;%PATH%"
start "" "%VTPLAY_EXE%" %*
