cmake_minimum_required(VERSION 3.10)

project(jetson_doctor
    VERSION 1.0.0
    DESCRIPTION "C++ Linux hardware diagnostic monitor for NVIDIA Jetson"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

add_executable(jetson_doctor
    src/main.cpp
    src/system_monitor.cpp
    src/report_generator.cpp)

# std::filesystem needs libstdc++fs on older GCC (< 9).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
    target_link_libraries(jetson_doctor stdc++fs)
endif()
