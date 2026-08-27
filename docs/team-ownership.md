<!-- 负责人：成员1：服务端架构/组长 -->
# 第一版四人代码归属

为了能立刻并行维护，第一版文件仍按原方案划分所有权。

| 成员 | 当前负责文件 | 第一版职责 |
|---|---|---|
| 成员1：服务端架构/组长 | 根 `CMakeLists.txt`、`CMakePresets.json`、`common/`、`ServerApp.*`、`SessionManager.*`、`server/src/main.cpp` | 公共协议、TCP、会话、请求分发、构建集成和协议测试 |
| 成员2：服务端存储 | `CloudRepository.*`、`SqliteApi.h`、`ServiceError.h`、`docs/database.md` | SQLite 表、目录树、blob、临时文件、哈希提交、秒传和引用删除 |
| 成员3：客户端网络 | `client_core/` | 请求响应、令牌状态、本地文件分块读写、下载临时文件和校验 |
| 成员4：客户端界面 | `client_cli/`、README 的操作流程 | 第一版 CLI 交互、Windows UTF-8 控制台和演示脚本；第二版迁移为 Qt Widgets |

公共协议文件由成员1最终合并；成员2、3不得复制消息枚举。成员4不得绕过 `ClientCore` 直接访问 socket。修改跨模块接口时，应同时更新调用方、协议文档和测试。

## 文件级唯一责任清单

每个受版本控制的源文件、构建脚本、测试和文档仅有一名负责人。可使用注释语法的文件，第一行均已加入 `负责人：成员X` 标注；严格 JSON 文件不支持注释，其归属在本清单中记录，其中 `CMakePresets.json` 同时使用 CMake 的 `vendor` 元数据保存归属。

| 成员 | 唯一负责的文件或目录 |
|---|---|
| 成员1：服务端架构/组长 | `.gitignore`、根 `CMakeLists.txt`、`CMakePresets.json`、`vcpkg.json`、`common/`、`server/CMakeLists.txt`、`ServerApp.*`、`SessionManager.*`、`server/src/main.cpp`、`tests/`、`docs/protocol.md`、`docs/iterations.md`、本文件、`BUILD_WINDOWS.md`、`build_release.bat`、`generate_vs2022_solution.bat`、`tools/` |
| 成员2：服务端存储 | `CloudRepository.*`、`SqliteApi.h`、`ServiceError.h`、`docs/database.md`、`服务端存储模块-技术调研与方案设计.md`、`runtime/config/server.json.example` |
| 成员3：客户端网络 | `client_core/CMakeLists.txt`、`client_core/include/cloud/client/ClientCore.h`、`client_core/src/ClientCore.cpp` |
| 成员4：客户端界面 | `client_cli/CMakeLists.txt`、`client_cli/src/main.cpp`、`README.md`、`packaging/run_server.bat`、`packaging/run_client_local.bat`、`packaging/run_client_lan.bat` |

`out/`、`dist/`、`tools/vcpkg/`、`runtime/cloud.db*`、`runtime/storage/`、`*.part` 和 `*.download` 都是生成文件或运行数据，不纳入源代码分工，也不得提交到仓库。
