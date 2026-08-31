# Minimal Qt example application that uses cloud_client_qt
if(NOT BUILD_QT_CLIENT)
  message(STATUS "BUILD_QT_CLIENT is OFF; skipping client_qt example")
  return()
endif()

find_package(Qt6 COMPONENTS Core Widgets REQUIRED)
set(CMAKE_AUTOMOC ON)

add_executable(client_qt_example
  src/main.cpp
  src/FileBrowser.cpp
  src/MainWindow.cpp
  src/TransferManager.cpp
  include/client_qt/FileBrowser.h
  include/client_qt/MainWindow.h
  include/client_qt/TransferManager.h
)

target_include_directories(client_qt_example PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../client_core/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(client_qt_example PRIVATE cloud_client_qt Qt6::Widgets)

target_compile_definitions(client_qt_example PRIVATE QT_CORE_LIB)
if(MSVC)
  target_compile_options(client_qt_example PRIVATE /utf-8)
endif()
set_target_properties(client_qt_example PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
