// 负责人：成员1：服务端架构/组长
#include "cloud/server/ServerApp.h"
#include "cloud/common/ExecutablePath.h"
#include "cloud/common/ProtocolConstants.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

int main(int argc,char** argv) {
    try {
        const std::uint16_t port=argc>1?static_cast<std::uint16_t>(std::stoul(argv[1])):cloud::common::kDefaultPort;
        const std::filesystem::path runtime=argc>2
            ? std::filesystem::path(argv[2])
            : cloud::common::executableDirectory()/"runtime";
        cloud::server::ServerApp app("0.0.0.0",port,runtime);
        app.run();
    } catch(const std::exception& e) {
        std::cerr<<"fatal server error: "<<e.what()<<'\n'; return 1;
    }
}
