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
- 浏览器端开源示例：[pilot_center/](pilot_center/)

## Web3 集成
本工程是一个开源 WebRTC SFU 服务端，支持 WebRTC 视频会议功能。

我们将 Ethereum 区块链作为视频会议的平台，覆盖以下场景：

* **创建会议** — 创建人需连接钱包，通过 UniswapV3 购买 WebRTC Meeting Token，支付创建会议的费用后，方可创建视频会议。
* **加入会议** — 与会人需连接钱包，通过 UniswapV3 购买 WebRTC Meeting Token，支付参与会议的费用后，方可加入视频会议。
* **ERC20 代币** — 用于支付视频会议费用。会议创建人支付少量会议 Token 创建会议；与会人支付会议 Token 加入会议。
* **UniswapV3** — 用于交换 ERC20 代币，实现会议费用支付：提供 Eth 到 ERC20 代币的兑换服务、ERC20 代币到 Eth 的兑换服务、会议 Token 与 Eth 的流动性添加与移除。

集成细节请参考 [web3dev.md](web3dev.md)。

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

## RTCPilot接入语音代理（`voice_agent`）
RTCPilot SFU支持接入语音代理（`voice_agent`），用于处理与AI大模型的语音交互。

**Voice Agent** 是一个先进的「实时语音对话 AI」智能体。voice_agent服务的开源地址: [https://github.com/runner365/VoiceAgent](https://github.com/runner365/VoiceAgent)

接入VoiceAgent的步骤如下：
1. 部署VoiceAgent服务，确保其正常运行。
2. 在RTCPilot配置文件中启用语音代理功能（`enable: true`）。
3. 配置VoiceAgent服务的IP地址、端口和注册路径前缀（`agent_ip`、`agent_port`、`subpath`）。
```yaml
voice_agent:
  enable: true
  agent_ip: "192.168.1.221"
  agent_port: 5555
  subpath: "/voiceagent"
```

详细见配置文件 `config_guide.md`（中文）和 `config_guide_en.md`（英文）中`voice_agent`部分。

## 配置
- 项目使用 YAML 文件进行配置（例如 `RTCPilot/config.yaml`）。
- 在运行前请根据网络、日志和 SFU 参数需求调整配置文件。
- 详细配置说明：请参阅 [config_guide.md](config_guide.md)（中文）和 [config_guide_en.md](config_guide_en.md)（英文）。

## 依赖要求
- 支持 C++17 或更新的编译器。
- 推荐使用 CMake 3.10 及以上进行跨平台构建。
- 平台相关的本地依赖：OpenSSL、libsrtp、libuv、yaml-cpp 等。详见 `3rdparty` 和 `win_3rdparty` 目录。



