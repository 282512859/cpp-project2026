# 负责人：成员3：客户端网络
add_library(cloud_client_core STATIC src/ClientCore.cpp)
target_include_directories(cloud_client_core PUBLIC include)
target_link_libraries(cloud_client_core PUBLIC cloud_common)

# Optional Qt wrapper for ClientCore (QThread + QObject)
option(BUILD_QT_CLIENT "Build Qt client wrapper (requires Qt6Core)" OFF)
if(BUILD_QT_CLIENT)
  set(CMAKE_AUTOMOC ON)
  find_package(Qt6 COMPONENTS Core REQUIRED)
  add_library(cloud_client_qt STATIC
    src/QtClient.cpp
    include/cloud/client/QtClient.h
  )
  target_include_directories(cloud_client_qt PUBLIC include)
  target_link_libraries(cloud_client_qt PUBLIC cloud_client_core Qt6::Core)
  target_compile_definitions(cloud_client_qt PUBLIC QT_CORE_LIB)
endif()
