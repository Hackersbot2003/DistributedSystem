cmake_minimum_required(VERSION 3.20)
project(DistributedFileStorage LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost CONFIG REQUIRED COMPONENTS system)

add_executable(dfs_server src/server.cpp)
add_executable(dfs_client src/client.cpp)
add_executable(dfs_coordinator_test src/coordinator_test.cpp)
add_executable(dfs_retrieve_test src/retrieve_test.cpp)

foreach(target dfs_server dfs_client dfs_coordinator_test dfs_retrieve_test)
    target_include_directories(${target} PRIVATE C:/vcpkg/installed/x64-windows/include)
    target_link_directories(${target} PRIVATE C:/vcpkg/installed/x64-windows/lib)
    target_link_libraries(${target} PRIVATE Boost::system libcrypto zstd)
    target_compile_definitions(${target} PRIVATE _WIN32_WINNT=0x0601)
endforeach()