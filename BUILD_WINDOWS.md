<!-- 负责人：成员1：服务端架构/组长 -->
# Visual Studio 2022 构建与免安装包

## 最快方法

在 Windows 10/11 x64 上安装以下软件：

- Visual Studio 2022，勾选“使用 C++ 的桌面开发”和 CMake 组件；
- Git for Windows；
- Python 3.10 或更高版本（仅构建后台转换组件时需要）；
- 网络连接（首次获取 vcpkg 和静态 SQLite）。

解压源码后双击：

```text
build_release.bat
```

脚本会依次：

1. 定位 VS2022 的 x64 编译工具；
2. 准备 vcpkg；
3. 使用 `x64-windows-static` 生成解决方案；
4. 以 `/MT` 编译 Release；
5. 执行公共层单元测试；
6. 检查两个文件是否为 Windows PE，并确认没有 SQLite/VC Runtime DLL 依赖；
7. 在 Windows 上自动启动服务端和客户端，完成注册、登录、建目录、上传、列表、下载及 SHA-256 对比；
8. 仅在全部验证通过后生成免安装压缩包。

成功输出：

```text
dist/LanCloudDrive_v0.1_windows_x64_portable.zip
out/build/windows-release/LanCloudDrive.sln
```

构建脚本还会生成并打包 `tools/document_converter.exe`。它包含 MarkItDown 的
DOCX/PDF 依赖，最终用户不需要安装 Python；首次构建需要连接 PyPI 下载这些依赖。

免安装包包含：

```text
LanCloudDrive_v0.1_windows_x64/
├─ bin/
│  ├─ cloud_server.exe
│  └─ cloud_client.exe
├─ runtime/
├─ docs/
├─ run_server.bat
├─ run_client_local.bat
└─ run_client_lan.bat
```

SQLite 和 MSVC C/C++ 运行库均静态链接；运行电脑不需要安装 Visual Studio、vcpkg 或 SQLite DLL。程序仍会使用 Windows 自带的 `ws2_32.dll` 等系统组件。

## 在 Visual Studio 2022 中打开

双击 `generate_vs2022_solution.bat`，脚本会生成并打开：

```text
out/build/windows-debug/LanCloudDrive.sln
```

解决方案包含：

- `cloud_common`
- `cloud_server`
- `cloud_client_core`
- `cloud_client`
- `cloud_common_tests`

也可以在 Visual Studio 2022 中选择“打开本地文件夹”，直接打开源码根目录；VS 会识别顶层 `CMakeLists.txt`。

## 运行验证

1. 解压免安装包；
2. 双击 `run_server.bat`；
3. 双击 `run_client_local.bat`；
4. 在客户端依次输入：

```text
register alice password123
login alice password123
mkdir 0 "课程资料"
ls 0
put "C:\Users\你的用户名\Desktop\test.txt" 1 "测试文件.txt"
ls 1
get 2 "C:\Users\你的用户名\Desktop\download.txt"
quit
```

跨电脑演示时，在服务端电脑放行 TCP 9000 入站规则，在客户端电脑双击 `run_client_lan.bat` 并输入服务端的 IPv4 地址。

## 常见错误

- `Visual Studio 2022 C++ tools were not found`：打开 Visual Studio Installer，安装“使用 C++ 的桌面开发”。
- `Git was not found`：安装 Git for Windows，重新打开命令行。
- vcpkg 下载失败：检查网络，或手动设置 `VCPKG_ROOT` 指向已有 vcpkg。
- 客户端无法连接：确认服务端窗口仍在运行、IP/端口正确，并检查 Windows 防火墙。

## 可选：使用 Qt 构建（用于 GUI 开发）

如果要在本仓库基础上开发 Qt GUI，新增了 CMake 选项 `BUILD_QT_CLIENT`（默认 OFF），它会编译 cloud_client_qt 静态库（包含 QtClient 封装，依赖 Qt6::Core）。

准备工作：
- 安装 Qt 6（推荐用 Qt Online Installer），选择与 Visual Studio 2022 对应的 MSVC 构建（例如 MSVC 2019/2022 x64，确保与本机编译器兼容）。
- 确保 CMake 能找到 Qt（通常安装后 CMake 能自动发现；如不能，设置环境变量 `Qt6_DIR` 或 `CMAKE_PREFIX_PATH` 指向 Qt 安装的 cmake 目录，或使用 vcpkg 的 Qt 包）。

使用预设（推荐）:

```powershell
cmake --preset windows-debug-qt
cmake --build --preset windows-debug-qt
```

或手工：

```powershell
cmake -S . -B out/build/windows-debug-qt -DBUILD_QT_CLIENT=ON -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake
cmake --build out/build/windows-debug-qt --config Debug
```

说明：
- `BUILD_QT_CLIENT` 只构建封装库 cloud_client_qt（Core 模块）。若需要完整 GUI，还需创建或添加一个 Qt 可执行目标并链接 cloud_client_qt，同时在 CMake 中 find_package(Qt6 COMPONENTS Widgets REQUIRED) 等。
- 使用 Qt 构建会增加依赖和构建时间；默认构建不启用以保持轻量。
