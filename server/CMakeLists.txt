# 负责人：成员1：服务端架构/组长
add_executable(cloud_server
    src/CloudRepository.cpp
    src/ServerApp.cpp
    src/SessionManager.cpp
    src/main.cpp
)
target_include_directories(cloud_server PRIVATE include)
target_link_libraries(cloud_server PRIVATE cloud_common)

find_package(SQLite3 QUIET)
if(TARGET SQLite::SQLite3)
    target_link_libraries(cloud_server PRIVATE SQLite::SQLite3)
else()
    find_library(LANCLOUD_SQLITE_LIBRARY NAMES sqlite3
        HINTS "${PROJECT_SOURCE_DIR}/third_party/sqlite/lib")
    if(NOT LANCLOUD_SQLITE_LIBRARY)
        message(FATAL_ERROR "SQLite library not found. Install it with: vcpkg install sqlite3:x64-windows")
    endif()
    target_link_libraries(cloud_server PRIVATE "${LANCLOUD_SQLITE_LIBRARY}")
endif()

install(TARGETS cloud_server RUNTIME DESTINATION bin)
