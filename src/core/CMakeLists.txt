# src/core — 纯 C++ 媒体管线，不依赖 Qt UI
add_library(vtcore STATIC
    src/MediaPipeline.cpp
    src/Demuxer.cpp
    src/Decoder.cpp
    src/Clock.cpp
    src/PacketQueue.cpp
    src/FrameQueue.cpp
    src/PlayerState.cpp
)

target_include_directories(vtcore PUBLIC include include/vtcore)

target_link_libraries(vtcore PUBLIC
    PkgConfig::AVFORMAT
    PkgConfig::AVCODEC
    PkgConfig::AVSWSCALE
    PkgConfig::AVSWRESAMPLE
    PkgConfig::AVUTIL
    Threads::Threads
)

target_compile_features(vtcore PUBLIC cxx_std_20)