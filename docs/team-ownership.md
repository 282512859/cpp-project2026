# 第一版四人代码归属

为了能立刻并行维护，第一版文件仍按原方案划分所有权。

| 成员 | 当前负责文件 | 第一版职责 |
|---|---|---|
| 成员1：服务端架构/组长 | 根 `CMakeLists.txt`、`CMakePresets.json`、`common/`、`ServerApp.*`、`SessionManager.*`、`server/src/main.cpp` | 公共协议、TCP、会话、请求分发、构建集成和协议测试 |
| 成员2：服务端存储 | `CloudRepository.*`、`SqliteApi.h`、`ServiceError.h`、`docs/database.md` | SQLite 表、目录树、blob、临时文件、哈希提交、秒传和引用删除 |
| 成员3：客户端网络 | `client_core/`、`docs/protocol.md` 的客户端流程 | 请求响应、令牌状态、本地文件分块读写、下载临时文件和校验 |
| 成员4：客户端界面 | `client_cli/`、README 的操作流程 | 第一版 CLI 交互、Windows UTF-8 控制台和演示脚本；第二版迁移为 Qt Widgets |

公共协议文件由成员1最终合并；成员2、3不得复制消息枚举。成员4不得绕过 `ClientCore` 直接访问 socket。修改跨模块接口时，应同时更新调用方、协议文档和测试。
