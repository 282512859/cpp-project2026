# 负责人：成员1：服务端架构/组长
add_executable(cloud_common_tests common/ProtocolCodecTests.cpp)
target_link_libraries(cloud_common_tests PRIVATE cloud_common)
add_test(NAME cloud_common_tests COMMAND cloud_common_tests)
