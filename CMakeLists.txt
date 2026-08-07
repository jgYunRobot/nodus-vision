cmake_minimum_required(VERSION 3.28)

project(nodus_vision VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CTest)

# Runtime and library targets are intentionally deferred to the approved migration checkpoints.
