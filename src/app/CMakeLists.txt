# src/app — Qt/QML 壳与播放器控制层

# ---- Windows 资源（应用图标 + 版本信息）----
# 版本号从 project(... VERSION ...) 注入，避免在多处硬编码。
set(VTPLAY_VERSION      "${PROJECT_VERSION}")
set(VTPLAY_RC_VERSION   "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
set(VTPLAY_ICON_PATH    "${CMAKE_SOURCE_DIR}/resources/icons/vtplay.ico")
set(VTPLAY_LOGO_PATH    "${CMAKE_SOURCE_DIR}/resources/icons/vtplay_128.png")

configure_file(vtplay.rc.in ${CMAKE_CURRENT_BINARY_DIR}/vtplay.rc @ONLY)

add_executable(vtplay
    src/main.cpp
    src/PlayerController.cpp
    src/VideoRenderer.cpp
    src/AudioOutput.cpp
    src/Exporter.cpp
    src/FilmDiagnosis.cpp
    src/RestorePlan.cpp
    src/RestoreRunner.cpp
    include/vtapp/PlayerController.h
    include/vtapp/VideoRenderer.h
    include/vtapp/AudioOutput.h
    include/vtapp/Exporter.h
    include/vtapp/FilmDiagnosis.h
    include/vtapp/RestorePlan.h
    include/vtapp/RestoreRunner.h
)

# 资源脚本仅 Windows 需要（MinGW 经 windres 自动编译）
if(WIN32)
    target_sources(vtplay PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/vtplay.rc)
endif()

target_include_directories(vtplay PRIVATE include include/vtapp)

# 版本号单一来源：C++ 侧通过 VTPLAY_VERSION 宏读取
target_compile_definitions(vtplay PRIVATE VTPLAY_VERSION="${PROJECT_VERSION}")

target_link_libraries(vtplay PRIVATE
    vtcore
    Qt5::Core
    Qt5::Gui
    Qt5::Quick
    Qt5::QuickControls2
    Qt5::Multimedia
)

# ---- QML 资源（Qt5：qrc + 手写 qmldir）----
# Qt5 没有 qt_add_qml_module（那是 Qt6 的模块系统）。这里改用显式 qrc，并把手写的
# qmldir 一并打进同一前缀，效果对齐 Qt6 的模块布局：
#   · 模块根固定为 qrc:/VTPlay，于是 QML 里的 `import VTPlay 1.0` 语义不变
#   · Theme 是 `pragma Singleton`，必须在 qmldir 里写 `singleton Theme 1.0 Theme.qml`
#     才会被注册（Qt6 靠 QT_QML_SINGLETON_TYPE 属性，Qt5 没有这套）
#   · 其余 .qml 在 qmldir 里逐个列出：目录里存在 qmldir 时，不再依赖隐式规则去猜
# 品牌 logo 的别名相应变为 qrc:/VTPlay/brand/logo.png（QML 侧同步改过）。
# AUTORCC 已在顶层打开，因此把 .qrc 加进目标即可。
target_sources(vtplay PRIVATE qml.qrc)

# 子系统：Debug 保留控制台便于查看诊断输出；Release 用窗口子系统（双击不弹黑窗）。
# 发布构建下仍可用 VTPLAY_CONSOLE=1 附加控制台（见 main.cpp）。
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_target_properties(vtplay PROPERTIES WIN32_EXECUTABLE FALSE)
else()
    set_target_properties(vtplay PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

set_target_properties(vtplay PROPERTIES MACOSX_BUNDLE TRUE)
