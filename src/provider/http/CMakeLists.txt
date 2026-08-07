include(ExternalProject)

set(libjpeg_turbo_binary_dir ${CMAKE_BINARY_DIR}/libjpeg_turbo)
ExternalProject_Add(
    nodus_vision_libjpeg_turbo_project
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/libs/libjpeg_turbo
    BINARY_DIR ${libjpeg_turbo_binary_dir}
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DENABLE_SHARED=OFF
        -DENABLE_STATIC=ON
        -DWITH_TURBOJPEG=OFF
        -DWITH_JPEG7=OFF
        -DWITH_JPEG8=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    BUILD_BYPRODUCTS ${libjpeg_turbo_binary_dir}/libjpeg.a
    INSTALL_COMMAND ""
)
add_library(nodus_vision_libjpeg_turbo STATIC IMPORTED GLOBAL)
set_target_properties(
    nodus_vision_libjpeg_turbo
    PROPERTIES
        IMPORTED_LOCATION ${libjpeg_turbo_binary_dir}/libjpeg.a
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/libs/libjpeg_turbo;${libjpeg_turbo_binary_dir}"
)
add_dependencies(nodus_vision_libjpeg_turbo nodus_vision_libjpeg_turbo_project)

add_library(nodus_vision_provider_http STATIC provider_http_server.cpp jpeg_encoder.cpp encoded_preview_cache.cpp pcd1.cpp)
target_include_directories(nodus_vision_provider_http PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    nodus_vision_provider_http
    PUBLIC
        nodus_vision_core
        Boost::system
        nodus_vision_libjpeg_turbo
)
target_compile_features(nodus_vision_provider_http PUBLIC cxx_std_17)
target_compile_options(nodus_vision_provider_http PRIVATE -Wall -Wextra)
