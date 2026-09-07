# 负责人：成员1：服务端架构/组长
add_executable(cloud_server
    src/CloudRepository.cpp
    src/MarkdownConverter.cpp
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

set(LANCLOUD_PDFTOPPM_EXE
    "${PROJECT_SOURCE_DIR}/out/tools/pdftoppm.exe"
    CACHE FILEPATH "Path to the pdftoppm PDF preview renderer")
if(EXISTS "${LANCLOUD_PDFTOPPM_EXE}")
    add_custom_command(TARGET cloud_server POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:cloud_server>/tools"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${LANCLOUD_PDFTOPPM_EXE}"
                "$<TARGET_FILE_DIR:cloud_server>/tools/pdftoppm.exe")
    install(PROGRAMS "${LANCLOUD_PDFTOPPM_EXE}" DESTINATION tools)
endif()

install(TARGETS cloud_server RUNTIME DESTINATION bin)

set(LANCLOUD_DOCUMENT_CONVERTER_EXE
    "${PROJECT_SOURCE_DIR}/out/tools/document_converter.exe"
    CACHE FILEPATH "Path to the packaged document converter helper")
if(EXISTS "${LANCLOUD_DOCUMENT_CONVERTER_EXE}")
    add_custom_command(TARGET cloud_server POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:cloud_server>/tools"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${LANCLOUD_DOCUMENT_CONVERTER_EXE}"
                "$<TARGET_FILE_DIR:cloud_server>/tools/document_converter.exe")
    install(PROGRAMS "${LANCLOUD_DOCUMENT_CONVERTER_EXE}" DESTINATION tools)
    install(FILES "${PROJECT_SOURCE_DIR}/out/tools/document_converter_licenses.txt"
            DESTINATION tools)
else()
    message(WARNING "document_converter.exe is absent; the convert command will be unavailable")
endif()
