# src/app — Qt/QML 壳与播放器控制层
add_executable(vtplay
    src/main.cpp
    src/PlayerController.cpp
    src/VideoRenderer.cpp
    src/AudioOutput.cpp
    include/vtapp/PlayerController.h
    include/vtapp/VideoRenderer.h
    include/vtapp/AudioOutput.h
)

target_include_directories(vtplay PRIVATE include include/vtapp)

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

qt_add_qml_module(vtplay
    URI VTPlay
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/VideoSurface.qml
        qml/ControlsBar.qml
        qml/Theme.qml
)
# Qt >= 6.8: qt_add_qml_module 自动扫描 QML_NAMED_ELEMENT 完成类型注册

set_target_properties(vtplay PROPERTIES
    WIN32_EXECUTABLE FALSE   # 控制台可见调试输出
    MACOSX_BUNDLE TRUE
)