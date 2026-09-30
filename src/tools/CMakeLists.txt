# src/tools — headless 管线自测（无 UI）：输出解码帧数、PTS 连续性等统计
# 用法：vtplay-pipeline-check <media-file>
add_executable(vtplay-pipeline-check src/pipeline_check.cpp)
target_link_libraries(vtplay-pipeline-check PRIVATE vtcore)
set_target_properties(vtplay-pipeline-check PROPERTIES WIN32_EXECUTABLE FALSE)

# src/tools — 导出链路 headless 自测：验证预设/命令行构造/进度/取消
# 直接编译 Exporter.cpp（而非抽出静态库），避免为单个工具增加构建复杂度。
# 用法：vtplay-export-check <media-file> [presetId] [--dry|--cancel-after N]
# 必须把 Exporter.h 也列入源：AUTOMOC 只对「目标源中列出的头文件」生成 moc，
# 而该头文件不与 Exporter.cpp 同目录，AUTOMOC 无法自动发现。
add_executable(vtplay-export-check
    src/export_check.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/Exporter.cpp
    ${CMAKE_SOURCE_DIR}/src/app/include/vtapp/Exporter.h
)
# 与 app 目标保持一致的包含路径（Exporter.h 位于 include/vtapp/）
target_include_directories(vtplay-export-check PRIVATE
    ${CMAKE_SOURCE_DIR}/src/app/include
    ${CMAKE_SOURCE_DIR}/src/app/include/vtapp
)
target_link_libraries(vtplay-export-check PRIVATE vtcore Qt6::Core)
set_target_properties(vtplay-export-check PROPERTIES
    AUTOMOC ON
    WIN32_EXECUTABLE FALSE
)
