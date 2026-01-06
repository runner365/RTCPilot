中文: [English](README.md)

# RTCPilot

RTCPilot 是一个使用现代 C++ 实现的开源 WebRTC SFU（Selective Forwarding Unit，选择性转发单元）。

## 主要特点
- 高性能的 WebRTC SFU，用于实时媒体转发。
- 跨平台支持：Windows 11、Linux（推荐 Debian）和 macOS。
- 支持 SFU 集群，便于横向扩展部署。
- 使用现代 C++ 开发，需 C++17 或更高版本进行编译。

## 仓库结构（节选）
- `src/` — SFU 以及辅助库的 C++ 源代码。
- `pilot_center/` — 使用 Python 编写的集群管理服务，负责 SFU 注册与信息转发。
- `3rdparty/`, `win_3rdparty/` — 第三方依赖和平台相关构建辅助文件。

## WebRTC JS 客户端
- 浏览器端开源示例：[https://github.com/runner365/webrtc_js_client](https://github.com/runner365/webrtc_js_client)

## 支持的平台与构建方式
### Windows 11（Visual Studio）
- 推荐：Visual Studio Community 2022（在 17.14.16 版本上有测试）。
- 打开仓库中的 Visual Studio 解决方案 `RTCPilot.sln`，选择 x64 的 Debug/Release 配置并进行构建。
- 请确保所需的第三方库（OpenSSL、libuv、libsrtp、yaml-cpp 等）已放置在 `win_3rdparty` 或已在系统中安装。

### Linux（推荐 Debian）
- 要求：支持 C++17 的编译器（gcc/clang）、`cmake` 以及常规构建工具。
- 示例构建步骤：

```bash
sudo apt update
sudo apt install -y build-essential cmake git libssl-dev
mkdir build && cd build
cmake .. 
make -j 2
```

### macOS
- 使用 CMake 构建（可采用 Xcode 或 clang 工具链）。
- 示例：

```bash
mkdir build && cd build
cmake ..
make -j 2
```

## 集群与 `pilot_center`
- `pilot_center` 目录包含用于集群管理的 Python 服务。
- 该服务负责 SFU 节点的注册，并在服务间转发 SFU 信息，以支持集群发现与调度。
- 请参考 `pilot_center/requirements.txt` 和 `pilot_center/pilot_center.py` 获取快速启动说明。

## 配置
- 项目使用 YAML 文件进行配置（例如 `RTCPilot/config.yaml`）。
- 在运行前请根据网络、日志和 SFU 参数需求调整配置文件。
- 详细配置说明：请参阅 [config_guide.md](config_guide.md)（中文）和 [config_guide_en.md](config_guide_en.md)（英文）。

## 依赖要求
- 支持 C++17 或更新的编译器。
- 推荐使用 CMake 3.10 及以上进行跨平台构建。
- 平台相关的本地依赖：OpenSSL、libsrtp、libuv、yaml-cpp 等。详见 `3rdparty` 和 `win_3rdparty` 目录。

## 贡献
- 欢迎贡献与问题反馈。请通过 issue 或 pull request 提交变更说明。

## 许可
- 有关许可信息请参阅仓库根目录的 `LICENSE` 文件。

## 联系方式
- 如需了解架构或集群管理相关问题，请查看 `pilot_center` 目录或提交 issue。
中文: [English](README.md)

