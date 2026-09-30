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
    include/vtapp/PlayerController.h
    include/vtapp/VideoRenderer.h
    include/vtapp/AudioOutput.h
    include/vtapp/Exporter.h
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
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::OpenGL
    Qt6::Multimedia
)

# Theme 是 QML 单例（pragma Singleton），需要显式标记才能写进 qmldir
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# 品牌标识（顶栏/关于页）：用别名固定资源路径，QML 侧引用
# qrc:/qt/qml/VTPlay/brand/logo.png —— 比依赖相对路径推导更稳
set_source_files_properties(${VTPLAY_LOGO_PATH} PROPERTIES QT_RESOURCE_ALIAS brand/logo.png)

qt_add_qml_module(vtplay
    URI VTPlay
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/VideoSurface.qml
        qml/ControlsBar.qml
        qml/InfoBar.qml
        qml/TopBar.qml
        qml/Sidebar.qml
        qml/AppMenu.qml
        qml/FlatButton.qml
        qml/Toast.qml
        qml/VolumeSlider.qml
        qml/SpeedMenu.qml
        qml/SettingsDialog.qml
        qml/DialogHeader.qml
        qml/ExportDialog.qml
        qml/ShortcutsDialog.qml
        qml/AboutDialog.qml
        qml/Theme.qml
    RESOURCES
        ${VTPLAY_LOGO_PATH}
)
# Qt >= 6.8: qt_add_qml_module 自动扫描 QML_NAMED_ELEMENT 完成类型注册

# 子系统：Debug 保留控制台便于查看诊断输出；Release 用窗口子系统（双击不弹黑窗）。
# 发布构建下仍可用 VTPLAY_CONSOLE=1 附加控制台（见 main.cpp）。
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_target_properties(vtplay PROPERTIES WIN32_EXECUTABLE FALSE)
else()
    set_target_properties(vtplay PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

set_target_properties(vtplay PROPERTIES MACOSX_BUNDLE TRUE)
