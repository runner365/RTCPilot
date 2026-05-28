中文 | [English](README.md)
# WebRTC JS Client (Vue 3)

基于 Vue 3 + Vite + TypeScript 的 WebRTC 前端示例，封装推拉流、房间信令与基础文字聊天，可作为客户端起点或调试工具。

## 功能概览
- 使用 `protoo-client` 进行 WebSocket 信令（默认地址可在 App 中修改）。
- `PCWrap` 对 `RTCPeerConnection` 进行封装，支持 sendonly/recvonly/sendrecv，负责开设备、创建 offer、拉流轨道管理等。
- 房间用户生命周期：加入/离开、推流列表、断线重连标记。
- 文本聊天与基础 UI 控制（静音、隐藏远端视频等）。

入口：`src/main.ts` 挂载 `src/App.vue`；核心逻辑位于 `src/peerConnectionWrap/` 与 `src/App.vue`。

## 环境要求
- Node.js ≥ 18（Vite 7 需要）。
- 已安装 npm。

## 快速开始
1) 安装依赖

   ```bash
   npm install
   ```

2) 启动本地开发（默认 http://localhost:5173/）

   ```bash
   npm run dev
   ```

3) 生产构建

   ```bash
   npm run build
   ```

4) 预览构建产物

   ```bash
   npm run preview
   ```

## 目录速览
- `src/App.vue`：房间逻辑、信令交互、聊天与远端流管理。
- `src/peerConnectionWrap/PCWrap.ts`：轻量 RTCPeerConnection 封装。
- `src/peerConnectionWrap/ProtooClientWrap.ts`：信令客户端包装。
- `src/style.css`：全局样式。

## 服务端开源地址
- 信令/媒体服务端：- Open-source signaling/media server: [https://github.com/runner365/RTCPilot](https://github.com/runner365/RTCPilot)

## 配置与调试
- 默认信令地址在 `src/App.vue` 的 `wsUrl` 常量，按实际服务器修改。
- 如需自定义 ICE 服务器或媒体约束，可在构造 `PCWrap` 时传入 `iceServers`、`mediaConstraints`。

## 常见问题
- 无法获取摄像头/麦克风：检查浏览器权限或使用 https。
- 推/拉流失败：确认信令地址、房间号、用户 ID/Name 与服务端配置一致。
