# 负责人：成员1：服务端架构/组长
cmake_minimum_required(VERSION 3.24)
project(LanCloudDrive VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LANCLOUD_BUILD_TESTS "Build first-version tests" ON)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
    # /MT and /MTd keep the release package independent of the VC runtime DLL.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

install(FILES README.md BUILD_WINDOWS.md DESTINATION .)
install(DIRECTORY docs DESTINATION .)
install(FILES runtime/config/server.json.example DESTINATION runtime/config)
install(FILES
    packaging/run_server.bat
    packaging/run_client_local.bat
    packaging/run_client_lan.bat
    DESTINATION .)

add_subdirectory(common)
add_subdirectory(server)
add_subdirectory(client_core)
add_subdirectory(client_cli)

if(LANCLOUD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
