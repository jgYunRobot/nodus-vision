add_library(
    nodus_vision_recording
    STATIC
    recording_contracts.cpp
    recording_manifest.cpp
    recording_manager.cpp
    recording_store.cpp
    color_video_writer.cpp
)
target_include_directories(nodus_vision_recording PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale)
target_link_libraries(nodus_vision_recording PUBLIC nodus_vision_core Boost::json PkgConfig::FFMPEG)
target_compile_features(nodus_vision_recording PUBLIC cxx_std_17)
target_compile_options(nodus_vision_recording PRIVATE -Wall -Wextra)
