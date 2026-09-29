# src/tools — headless 管线自测（无 UI）：输出解码帧数、PTS 连续性等统计
# 用法：vtplay-pipeline-check <media-file>
add_executable(vtplay-pipeline-check src/pipeline_check.cpp)
target_link_libraries(vtplay-pipeline-check PRIVATE vtcore)
