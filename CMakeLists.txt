cmake_minimum_required(VERSION 3.20)
project(websocklib VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Dependencies (resolved by Conan) ----
find_package(Boost REQUIRED)
find_package(GTest REQUIRED)

# ---- Library ----
add_library(websocklib
    src/websocket.cpp
)

target_include_directories(websocklib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(websocklib
    PUBLIC
        Boost::headers
)

target_compile_options(websocklib PRIVATE -Wall -Wextra -Wpedantic)

# ---- Tests ----
enable_testing()
add_subdirectory(tests)
