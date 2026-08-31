# Minimal Qt example application that uses cloud_client_qt
if(NOT BUILD_QT_CLIENT)
  message(STATUS "BUILD_QT_CLIENT is OFF; skipping client_qt example")
  return()
endif()

find_package(Qt6 COMPONENTS Core Widgets REQUIRED)

add_executable(client_qt_example
  src/main.cpp
  src/FileBrowser.cpp
)

target_include_directories(client_qt_example PRIVATE ${CMAKE_SOURCE_DIR}/client_core/include ${CMAKE_SOURCE_DIR}/client_qt/include)
target_link_libraries(client_qt_example PRIVATE cloud_client_qt Qt6::Widgets)

target_compile_definitions(client_qt_example PRIVATE QT_CORE_LIB)
set_target_properties(client_qt_example PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
