add_library(
    nodus_vision_fake_adapter
    STATIC
    fake_camera_adapter.cpp
)
target_include_directories(
    nodus_vision_fake_adapter
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(nodus_vision_fake_adapter PUBLIC nodus_vision_core)
target_compile_features(nodus_vision_fake_adapter PUBLIC cxx_std_17)
target_compile_options(nodus_vision_fake_adapter PRIVATE -Wall -Wextra)
