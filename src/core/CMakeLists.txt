add_library(
    nodus_vision_core
    STATIC
    camera_contracts.cpp
    frame_store.cpp
)

target_include_directories(
    nodus_vision_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(nodus_vision_core PUBLIC cxx_std_17)
target_compile_options(nodus_vision_core PRIVATE -Wall -Wextra)

install(
    TARGETS nodus_vision_core
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(
    DIRECTORY ${CMAKE_SOURCE_DIR}/include/nodus_vision
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
