# 负责人：成员1：服务端架构/组长
add_library(cloud_common STATIC
    src/ExecutablePath.cpp
    src/JsonLite.cpp
    src/Packet.cpp
    src/ProtocolCodec.cpp
    src/Sha256.cpp
    src/Socket.cpp
)
target_include_directories(cloud_common PUBLIC include)
if(WIN32)
    target_link_libraries(cloud_common PUBLIC ws2_32)
endif()
