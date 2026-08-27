# 负责人：成员4：客户端界面
add_executable(cloud_client src/main.cpp)
target_link_libraries(cloud_client PRIVATE cloud_client_core)
install(TARGETS cloud_client RUNTIME DESTINATION bin)
