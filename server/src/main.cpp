// 负责人：成员1：服务端架构/组长
#include "cloud/server/ServerApp.h"
#include "cloud/common/ExecutablePath.h"
#include "cloud/common/ProtocolConstants.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

// 命令行格式：cloud_server.exe [端口] [运行数据目录]
int main(int argc, char** argv) {
    try {
        // argc 包含程序名，所以 argc > 1 才表示用户传入了第一个参数。
        const std::uint16_t port = argc > 1
            ? static_cast<std::uint16_t>(std::stoul(argv[1]))
            : cloud::common::kDefaultPort;

        // 未指定数据目录时，使用 exe 旁的 runtime，便于免安装包直接运行。
        const std::filesystem::path runtime = argc > 2
            ? std::filesystem::path(argv[2])
            : cloud::common::executableDirectory() / "runtime";

        // 0.0.0.0 表示监听本机所有 IPv4 网卡，使同一局域网的客户端能够连接。
        cloud::server::ServerApp app("0.0.0.0", port, runtime);
        app.run();
    } catch (const std::exception& e) {
        // main 是最后一道异常边界：记录致命错误，并用非 0 返回值通知操作系统启动失败。
        std::cerr << "fatal server error: " << e.what() << '\n';
        return 1;
    }
}
