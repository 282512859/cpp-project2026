<!-- 负责人：成员4：客户端界面 -->
# LanCloudDrive 第一版（v0.1）

这是一个 C++ 局域网网盘项目：提供两个独立的 Windows x64 控制台程序 `cloud_server.exe` 和 `cloud_client.exe`，实现 C/S 架构、公共协议、持久化、文件传输以及轻量文档转换。工程面向 Visual Studio 2022，可生成免安装运行包。

## 已实现功能

- TCP 客户端/服务端，服务端采用“每连接一线程”；
- 固定 24 字节网络序报文头，可靠处理分包与粘包；
- 用户注册、登录、注销与随机会话令牌；
- SQLite 持久化，用户数据互相隔离；
- 根目录/子目录浏览、新建目录、重命名、递归删除；
- 文件按 256 KiB 顺序分块上传、下载和进度输出；
- 服务端离线把 `.docx` 和文字型 `.pdf` 转换为同目录下的 Markdown 文件；
- 上传先写 `.part`，完成后重新计算 SHA-256，再提交文件节点；
- 按 `SHA-256 + size` 内容去重，相同文件再次上传可秒传；
- 下载先写 `.download`，大小及 SHA-256 校验成功后才改为目标文件；
- 中文远端文件名、空文件、错误密码、重名和越权访问的基本处理；
- 协议编解码、粘包/半包、SHA-256、JSON 的无框架单元测试。

## 第一版的边界

本版客户端是命令行程序 `cloud_client`。尚未实现 Qt 图形界面、GUI/网络异步桥接、每任务独立连接、取消与重试、断点续传、心跳超时、配额、PBKDF2、TLS、扫描 PDF 的 OCR 和视频快照。

本版仍遵守最终工程的核心边界：公共协议只有 `cloud_common` 一份；服务端 socket、业务和 SQLite 分层；客户端通过 `ClientCore` 访问网络，后续 Qt 界面无需接触裸 socket。

> 安全说明：目前口令经 TCP 明文传输，数据库内仅保存随机盐加 SHA-256 的演示级摘要。只可用于可信局域网课程演示，不可直接部署到公网或保存真实敏感文件。

## 一键生成 VS2022 解决方案和免安装包

安装 Visual Studio 2022“使用 C++ 的桌面开发”和 Git for Windows 后，双击源码根目录中的：

```text
build_release.bat
```

脚本将自动准备 vcpkg/SQLite、生成 VS2022 解决方案、静态编译、执行测试并输出：

```text
dist/LanCloudDrive_v0.1_windows_x64_portable.zip
out/build/windows-release/LanCloudDrive.sln
```

免安装包中的 SQLite 和 MSVC 运行库已经静态链接，验证电脑不需要安装 Visual Studio 或 SQLite DLL。完整说明见 [BUILD_WINDOWS.md](BUILD_WINDOWS.md)。

## 手动 Windows 构建环境

需要：

1. Visual Studio 2022，安装“使用 C++ 的桌面开发”；
2. CMake 3.24 或更高版本；
3. Git；
4. vcpkg；SQLite3 由 `vcpkg.json` 自动安装并静态链接。

首次安装 vcpkg：

```powershell
git clone https://github.com/microsoft/vcpkg C:\dev\vcpkg
C:\dev\vcpkg\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = "C:\dev\vcpkg"
```

在本项目根目录构建：

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

`vcpkg.json` 会声明 SQLite3 依赖，预设固定使用 `x64-windows-static` 和 MSVC `/MT`。若团队不用清单模式，也可先执行：

```powershell
C:\dev\vcpkg\vcpkg.exe install sqlite3:x64-windows-static
```

生成文件默认位于：

```text
out/build/windows-debug/Debug/cloud_server.exe
out/build/windows-debug/Debug/cloud_client.exe
```

Release 构建使用：

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
```

## 启动与演示

免安装包本机验证时，先双击 `run_server.bat`，再双击 `run_client_local.bat`。

也可以手动启动。在电脑 A 开放 Windows 防火墙 TCP 9000 端口后运行服务端：

```powershell
.\out\build\windows-debug\Debug\cloud_server.exe 9000 .\runtime
```

在电脑 A 本机或同一局域网电脑 B 启动客户端；将 `192.168.1.10` 换成服务端局域网 IPv4 地址，也可以使用免安装包中的 `run_client_lan.bat`：

```powershell
.\out\build\windows-debug\Debug\cloud_client.exe 192.168.1.10 9000
```

推荐演示命令：

```text
register alice password123
login alice password123
mkdir 0 "课程资料"
put "C:\Users\Alice\Desktop\报告.pdf" 1 "报告.pdf"
ls 1
get 2 "C:\Users\Alice\Desktop\下载的报告.pdf"
convert 2 "报告.md"
rename 2 "最终报告.pdf"
rm 2
logout
quit
```

根目录 ID 固定使用 `0`。`mkdir`、`put` 返回的新节点 ID 会因数据库内容而变化，请使用程序实际输出的 ID。

含空格的名称或路径必须放在双引号中。口令不要包含空格；第一版 CLI 不支持带空格口令。

## 命令表

| 命令 | 含义 |
|---|---|
| `register <用户名> <口令>` | 注册；用户名 3～32 字节，口令 8～128 字节 |
| `login <用户名> <口令>` | 登录并保存本次进程内令牌 |
| `ls [父目录ID]` | 列目录；不传参数时列根目录 |
| `mkdir <父目录ID> "名称"` | 新建目录 |
| `rename <节点ID> "新名称"` | 重命名文件或目录 |
| `rm <节点ID>` | 删除文件或递归删除目录 |
| `put "本地路径" <父目录ID> ["远端名称"]` | 上传文件 |
| `get <节点ID> "本地路径"` | 下载文件；拒绝覆盖已有目标 |
| `convert <节点ID> ["输出名称.md"]` | 在服务端把 `.docx`/`.pdf` 转换为同目录 Markdown |
| `logout` | 注销当前令牌 |
| `help` / `quit` | 显示帮助 / 退出 |

## 运行时数据

转换功能由发布包 `tools/document_converter.exe` 在服务端后台执行。用户无需安装
Python，也不要直接运行该辅助程序。转换使用 Microsoft MarkItDown 0.1.7，相关
第三方许可证位于 `tools/document_converter_licenses.txt`。

```text
runtime/
├─ cloud.db                 # SQLite 元数据
└─ storage/
   ├─ blobs/ab/cd/<sha256>  # 内容寻址文件实体
   └─ temp/<transfer>.part  # 未完成上传
```

用户提供的文件名不会参与服务端真实文件路径拼接，因此 `../` 等路径穿越名称会被拒绝。运行时数据库、blob 和临时文件已加入 `.gitignore`，不得提交到仓库。

## 工程结构

```text
LanCloudDrive/
├─ common/       cloud_common：协议、JSON、SHA-256、TCP 封装
├─ server/       cloud_server：会话、SQLite、目录与传输业务
├─ client_core/  cloud_client_core：同步客户端 API 与传输校验
├─ client_cli/   cloud_client：第一版命令行界面
├─ tests/        无第三方测试框架的公共层测试
├─ docs/         协议、数据库和迭代说明
└─ runtime/      本地运行数据（首次运行自动创建）
```

## 第一版现场验收建议

1. 两个客户端分别注册用户，证明根目录互相不可见；
2. 上传带中文名的文件并显示进度；
3. 下载到新路径，使用 `certutil -hashfile 文件 SHA256` 对比哈希；
4. 使用不同远端名称再次上传同一内容，观察立即完成，并确认 blobs 中仍只有一份实体；
5. 重启服务端后再次登录，确认目录和文件仍存在；
6. 演示错误密码、同目录重名、越权节点 ID 和下载目标已存在时的错误。

详细协议见 [docs/protocol.md](docs/protocol.md)，数据库见 [docs/database.md](docs/database.md)，后续计划见 [docs/iterations.md](docs/iterations.md)，四人维护边界见 [docs/team-ownership.md](docs/team-ownership.md)。
