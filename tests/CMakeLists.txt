# 负责人：成员1：服务端架构/组长
add_executable(cloud_common_tests common/ProtocolCodecTests.cpp)
target_link_libraries(cloud_common_tests PRIVATE cloud_common)
add_test(NAME cloud_common_tests COMMAND cloud_common_tests)

# 扩展功能端到端测试：需要先手动启动 cloud_server，再把端口传给测试程序。
# 例如：cloud_server 9200 <runtime>；set LANCLOUD_EXTENDED_TEST_PORT=9200；ctest
add_executable(cloud_extended_tests extended/ExtendedFeatureTests.cpp)
target_link_libraries(cloud_extended_tests PRIVATE cloud_client_core)
if(DEFINED ENV{LANCLOUD_EXTENDED_TEST_PORT})
    add_test(NAME cloud_extended_tests
             COMMAND cloud_extended_tests $ENV{LANCLOUD_EXTENDED_TEST_PORT})
endif()
