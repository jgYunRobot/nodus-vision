add_library(
    nodus_vision_recording
    STATIC
    recording_contracts.cpp
    recording_store.cpp
)
target_include_directories(nodus_vision_recording PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nodus_vision_recording PUBLIC Boost::json)
target_compile_features(nodus_vision_recording PUBLIC cxx_std_17)
target_compile_options(nodus_vision_recording PRIVATE -Wall -Wextra)
