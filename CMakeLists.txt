cmake_minimum_required(VERSION 3.20)

project(DistributedFileStorage LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost CONFIG REQUIRED COMPONENTS system)

add_executable(dfs_main
    src/main.cpp
)

target_include_directories(dfs_main PRIVATE
    C:/vcpkg/installed/x64-windows/include
)

target_link_directories(dfs_main PRIVATE
    C:/vcpkg/installed/x64-windows/lib
)

target_link_libraries(dfs_main PRIVATE
    Boost::system
    libcrypto
)