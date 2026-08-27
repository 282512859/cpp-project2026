# Visual Studio 2022 构建与免安装包

## 最快方法

在 Windows 10/11 x64 上安装以下软件：

- Visual Studio 2022，勾选“使用 C++ 的桌面开发”和 CMake 组件；
- Git for Windows；
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
