cmake_minimum_required(VERSION 3.15)
project(CppMiniServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Create executable (main.cpp is placeholder for now)
add_executable(server src/main.cpp)
target_include_directories(server PRIVATE include)
