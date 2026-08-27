# 负责人：成员3：客户端网络
add_library(cloud_client_core STATIC src/ClientCore.cpp)
target_include_directories(cloud_client_core PUBLIC include)
target_link_libraries(cloud_client_core PUBLIC cloud_common)
