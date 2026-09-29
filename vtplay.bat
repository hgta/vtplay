@echo off
REM ============================================================
REM  VTPlay launcher (development)
REM  MinGW runtime dir: default D:\dev\msys64\mingw64\bin
REM  Override with env vars VTPLAY_MINGW_BIN / VTPLAY_EXE
REM ============================================================
chcp 65001 >nul 2>&1
setlocal
if "%VTPLAY_MINGW_BIN%"=="" set "VTPLAY_MINGW_BIN=D:\dev\msys64\mingw64\bin"
if "%VTPLAY_EXE%"=="" set "VTPLAY_EXE=%~dp0build\src\app\vtplay.exe"
set "PATH=%VTPLAY_MINGW_BIN%;%PATH%"

if not exist "%VTPLAY_EXE%" (
    echo [ERROR] executable not found: %VTPLAY_EXE%
    echo         run: cmake --build build
    pause
    exit /b 1
)

echo [VTPlay] exe : %VTPLAY_EXE%
for %%F in ("%VTPLAY_EXE%") do echo [VTPlay] time: %%~tF
echo.

REM 直接运行（不用 start）：保留控制台输出，便于确认版本与排查问题
"%VTPLAY_EXE%" %*
