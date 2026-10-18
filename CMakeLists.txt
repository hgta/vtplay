cmake_minimum_required(VERSION 3.21)
# 版本号单一来源：注入为 VTPLAY_VERSION 宏与 Windows 资源版本信息
project(vtplay VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(WIN32)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

# ---- Dependencies ----
# Qt 6: Core, Gui, Quick, QuickControls2, OpenGL, Multimedia (for QAudioSink)
# legacy/win81 分支：为 Windows 8.1 x64 使用 Qt 5.15。
# Qt6 的 Qt6Gui 硬导入 d3d12.dll、Qt6Core 硬导入 SetThreadDescription（均 Win10 才有），
# 在 8.1 上进程会在 main() 之前失败；Qt5.15 实测无这两处导入。见
# openspec/changes/add-win81-legacy-build/design.md 的实测证据表。
find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Multimedia)

# FFmpeg 动态链接：vcpkg/系统包提供。
# 注意：MSYS2 的 mingw-w64-x86_64-ffmpeg 是 **GPL 构建**（--enable-gpl
# --enable-libx264/x265）。若目标是 LGPL 分发，需自行提供 LGPL 构建。
# 详见 README 的许可证章节。
# Threads::Threads 被 src/core 使用；Qt6 会把它间接带进作用域，Qt5 不会，
# 因此这里显式声明（也是正确做法：不要依赖别的包的传递依赖）。
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(AVFORMAT     REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVCODEC      REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVSWSCALE    REQUIRED IMPORTED_TARGET libswscale)
pkg_check_modules(AVSWRESAMPLE REQUIRED IMPORTED_TARGET libswresample)
pkg_check_modules(AVUTIL       REQUIRED IMPORTED_TARGET libavutil)

# 注：Qt6 的 `qt_policy(SET QTP0001 NEW)`（把 QML 模块放到 /qt/qml 前缀）在 Qt5 不存在，
# 本分支用显式 qrc 控制资源布局，模块根固定为 qrc:/VTPlay（见 src/app/qml.qrc）。

# ---- Sub-projects ----
add_subdirectory(src/core)
add_subdirectory(src/app)

option(VTPLAY_BUILD_TOOLS "Build headless pipeline self-test tool" ON)
if(VTPLAY_BUILD_TOOLS)
    add_subdirectory(src/tools)
endif()