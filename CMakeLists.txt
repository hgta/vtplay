cmake_minimum_required(VERSION 3.21)
# 版本号单一来源：注入为 VTPLAY_VERSION 宏与 Windows 资源版本信息
project(vtplay VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(WIN32)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

# ---- Dependencies ----
# Qt 6: Core, Gui, Quick, QuickControls2, OpenGL, Multimedia (for QAudioSink)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick QuickControls2 OpenGL Multimedia)

# FFmpeg 动态链接：vcpkg/系统包提供。
# 注意：MSYS2 的 mingw-w64-x86_64-ffmpeg 是 **GPL 构建**（--enable-gpl
# --enable-libx264/x265）。若目标是 LGPL 分发，需自行提供 LGPL 构建。
# 详见 README 的许可证章节。
find_package(PkgConfig REQUIRED)
pkg_check_modules(AVFORMAT     REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVCODEC      REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVSWSCALE    REQUIRED IMPORTED_TARGET libswscale)
pkg_check_modules(AVSWRESAMPLE REQUIRED IMPORTED_TARGET libswresample)
pkg_check_modules(AVUTIL       REQUIRED IMPORTED_TARGET libavutil)

# Qt6 QML policy：新式默认资源前缀 `/qt/qml`。
# 必须在 add_subdirectory(src/app) **之前**设置——策略只对设置之后创建的目标生效，
# 放后面会让模块落在 `/VTPlay/...`，QML 里按 `/qt/qml/...` 引用资源就会 404。
qt_policy(SET QTP0001 NEW)

# ---- Sub-projects ----
add_subdirectory(src/core)
add_subdirectory(src/app)

option(VTPLAY_BUILD_TOOLS "Build headless pipeline self-test tool" ON)
if(VTPLAY_BUILD_TOOLS)
    add_subdirectory(src/tools)
endif()