cmake_minimum_required(VERSION 3.28)

project(nodus_vision VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CTest)

option(NODUS_VISION_BUILD_TESTING "Build nodus-vision tests." ${BUILD_TESTING})

add_subdirectory(src)

if(NODUS_VISION_BUILD_TESTING)
    add_subdirectory(libs/googletest EXCLUDE_FROM_ALL)
    add_subdirectory(tests)
endif()
