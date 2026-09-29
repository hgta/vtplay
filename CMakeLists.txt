cmake_minimum_required(VERSION 3.21)
project(vtplay VERSION 0.1.0 LANGUAGES CXX)

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

# FFmpeg LGPL 动态链接：vcpkg/系统包提供
find_package(PkgConfig REQUIRED)
pkg_check_modules(AVFORMAT     REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVCODEC      REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVSWSCALE    REQUIRED IMPORTED_TARGET libswscale)
pkg_check_modules(AVSWRESAMPLE REQUIRED IMPORTED_TARGET libswresample)
pkg_check_modules(AVUTIL       REQUIRED IMPORTED_TARGET libavutil)

# ---- Sub-projects ----
add_subdirectory(src/core)
add_subdirectory(src/app)

# Qt6 QML policy: newer QTP0001 default path
qt_policy(SET QTP0001 NEW)

option(VTPLAY_BUILD_TOOLS "Build headless pipeline self-test tool" ON)
if(VTPLAY_BUILD_TOOLS)
    add_subdirectory(src/tools)
endif()