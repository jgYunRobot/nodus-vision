add_library(nodus_vision_config STATIC vision_config.cpp)
target_include_directories(nodus_vision_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    nodus_vision_config
    PUBLIC
        nodus_vision_core
        nodus_vision_fake_adapter
        nodus_vision_intel_d435
        Boost::json
)
target_compile_features(nodus_vision_config PUBLIC cxx_std_17)
target_compile_options(nodus_vision_config PRIVATE -Wall -Wextra)
