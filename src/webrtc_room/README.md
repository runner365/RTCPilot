# WebRTC Room 模块

## 目录结构

```
webrtc_room/
├── room_mgr.cpp/hpp          # 房间管理器（单例）
├── room.cpp/hpp              # 单个房间，RTP 转发中枢
├── webrtc_session.cpp/hpp    # WebRTC 连接会话（ICE/DTLS/SRTP）
├── webrtc_server.cpp/hpp     # WebRTC UDP 服务器（STUN 路由）
├── media_pusher.cpp/hpp      # 推流者（接收端）
├── media_puller.cpp/hpp      # 拉流者（发送端）
├── rtp_recv_session.cpp/hpp  # RTP 接收会话（丢包检测/NACK/RR）
├── rtp_send_session.cpp/hpp  # RTP 发送会话（RTX 重传/SR）
├── rtp_session.cpp/hpp       # RTP 会话基类（序列号管理/抖动计算）
├── rtc_recv_relay.cpp/hpp    # 远程流接收 Relay（UDP 收流）
├── rtc_send_relay.cpp/hpp    # 远程流发送 Relay（UDP 推流）
├── rtc_user.cpp/hpp          # 房间用户信息
├── rtc_info.hpp              # 核心数据结构（RtpSessionParam/PushInfo/PullRequestInfo）
├── dtls_session.cpp/hpp      # DTLS-SRTP 握手
├── srtp_session.cpp/hpp      # SRTP 加密/解密（基于 libsrtp2）
├── ice_server.cpp/hpp        # ICE STUN 处理
├── nack_generator.cpp/hpp    # NACK 丢包重传请求生成器
├── tcc_server.cpp/hpp        # Transport-wide CC 反馈
├── fast_jitterbuffer.cpp/hpp # 音频包 JitterBuffer（VoiceAgent 使用）
├── pilot_message_client.cpp/hpp # 与 PilotCenter 的 WebSocket 通信
├── whip.cpp/hpp              # WHIP（WebRTC-HTTP Ingestion）支持
├── udp_transport.hpp         # UDP 传输回调接口定义
├── port_generator.cpp/hpp    # UDP 端口分配器
└── voice_agent/              # 语音 AI 代理（ASR + TTS）
    ├── voice_agent.cpp/hpp   # 语音代理主体
    ├── voice_agent_pub.hpp   # 回调接口定义
    ├── va_fake_session.cpp/hpp # 模拟推流会话（AI 音频注入）
```

---

## 整体架构

按从外到内、从上到下的调用关系，共分四层：

```
┌──────────────────────────────────────────────┐
│  RoomMgr（房间管理器，单例）                    │
│  - 处理 WS Protoo 协议请求/通知                │
│  - 管理 rooms_（房间集合）                     │
│  - 与 PilotCenter 保持心跳                    │
│  - 处理 join / push / pull / heartbeat /     │
│    textMessage                               │
└────────────────┬─────────────────────────────┘
                 │
┌────────────────▼─────────────────────────────┐
│  Room（单个房间）                              │
│  - 管理 users_（RtcUser 集合）                │
│  - 管理 pusherId2pusher_（MediaPusher）       │
│  - 管理 pusher2pullers_（MediaPuller 扇形分发）│
│  - 管理 Relay（RtcRecvRelay / RtcSendRelay）  │
│  - RTP 包从 Pusher → Puller 的转发中枢        │
│  - VoiceAgent 语音识别/TTS 集成               │
└────────────────┬─────────────────────────────┘
                 │
┌────────────────▼─────────────────────────────┐
│  WebRtcSession（单个 WebRTC 连接）             │
│  - ICE（STUN）→ DTLS → SRTP 完整握手          │
│  - 管理 ssrc → MediaPusher/MediaPuller 映射   │
│  - 处理 RTP/RTCP 包分发                       │
│  - RTCP: SR / RR / NACK / PLI / TCC          │
└────────────────┬─────────────────────────────┘
                 │
┌────────────────▼─────────────────────────────┐
│  RTP 层：RtpRecvSession / RtpSendSession     │
│  - 序列号管理、丢包检测、NACK 生成             │
│  - RTCP RR（接收端报告）/ SR（发送端报告）     │
│  - RTX 重传缓存                               │
│  - Jitter 计算                                │
└──────────────────────────────────────────────┘
```

---

## 核心类职责

### 1. RoomMgr — 房间管理器（单例）

- 实现 `ProtooCallBackI`，处理来自客户端的 WebSocket Protoo 请求和通知
- `GetOrCreateRoom(room_id)` — 懒加载创建房间
- `OnProtooRequest()` — 处理 `join` / `push` / `pull` / `heartbeat` 请求
- `OnProtooNotification()` — 处理 `textMessage` 通知，转发给对应 Room
- `OnAsyncNotification()` — 接收来自 PilotCenter 的异步通知并转发：
  - `newUser` / `newPusher` / `pullRemoteStream` / `userDisconnect` / `userLeave` / `textMessage`
- `OnTimer()` — 定时清理不活跃的房间；向 PilotCenter 发送心跳

### 2. Room — 核心转发中枢

#### 成员映射关系

| 成员变量 | 类型 | 用途 |
|---------|------|------|
| `users_` | `map<user_id, RtcUser>` | 房间内所有用户 |
| `pusherId2pusher_` | `map<pusher_id, MediaPusher>` | 本地推流者 |
| `pusher2pullers_` | `map<pusher_id, map<puller_id, MediaPuller>>` | 一个推流对应多个拉流（扇形分发） |
| `pusherId2recvRelay_` | `map<pusher_id, RtcRecvRelay>` | 接收远程流的 UDP Relay |
| `pusher_user_id2recvRelay_` | `map<user_id, RtcRecvRelay>` | 用户 → 接收 Relay 映射 |
| `pusher_user_id2sendRelay_` | `map<user_id, RtcSendRelay>` | 用户 → 发送 Relay 映射 |

#### 关键流程

##### Push 流程（客户端推流）

```
客户端 WS Protoo "push" 请求
       │
       ▼
RoomMgr::HandlePushRequest()
       │
       ▼
Room::HandlePushSdp()
       │
       ├── 1. RtcSdp::ParseSdp() 解析 Offer SDP
       ├── 2. 创建 WebRtcSession（SRTP_SESSION_TYPE_RECV 方向）
       ├── 3. DtlsInit(Role::ROLE_SERVER) DTLS 握手初始化
       ├── 4. GenAnswerSdp() 生成 Answer SDP
       ├── 5. 从配置添加 ICE Candidate 到 Answer SDP
       ├── 6. GetRtpSessionParamsFromSdp() 提取 RTP 参数
       ├── 7. AddPusherRtpSession() 创建 MediaPusher
       ├── 8. 注册到 pusherId2pusher_, 设置 VoiceAgent 回调
       ├── 9. 返回 Answer SDP 给客户端
       ├── 10. NotifyNewPusher() 通知房间内其他用户
       └── 11. NewPusher2PilotCenter() 通知 PilotCenter
```

##### Pull 流程（客户端拉流）

```
客户端 WS Protoo "pull" 请求
       │
       ▼
RoomMgr::HandlePullRequest()
       │
       ├── 判断 target_user 类型：
       │   ├── LOCAL_RTC_USER  → Room::HandlePullSdp()
       │   └── REMOTE_RTC_USER → Room::HandleRemotePullSdp()
       │
       ▼
Room::HandlePullSdp()
       │
       ├── 1. RtcSdp::ParseSdp() 解析 Offer SDP
       ├── 2. 创建 WebRtcSession（SRTP_SESSION_TYPE_SEND 方向）
       ├── 3. DtlsInit(Role::ROLE_SERVER) DTLS 初始化
       ├── 4. GenAnswerSdp() 生成 Answer SDP
       ├── 5. 根据 pull_info.pushers_ 找到对应 MediaPusher
       ├── 6. AddPullerRtpSession() 创建 MediaPuller
       ├── 7. UpdateRtcSdpByPullers() 用 Puller 的参数更新 SDP
       ├── 8. 注册到 pusher2pullers_[pusher_id][puller_id]
       └── 9. 返回 Answer SDP 给客户端
```

##### RemotePull 流程（跨机器拉流）

```
Room::HandleRemotePullSdp()
       │
       ├── 1. 从 target_user 获取 PushInfo
       ├── 2. PullRemotePusher()
       │   ├── CreateOrGetRecvRtcRelay() 创建/复用 RtcRecvRelay
       │   │   └── RtcRecvRelay 开启 UDP 监听端口
       │   └── SendPullRequestToPilotCenter()
       │       └── 告知远端 PilotCenter 向本机 UDP 推流
       ├── 3. 创建 WebRtcSession（SRTP_SESSION_TYPE_SEND）
       ├── 4. 从 RtcRecvRelay 获取 PushInfo → 创建 MediaPuller
       ├── 5. UpdateRtcSdpByPullers() 更新 SDP
       └── 6. 注册到 pusher2pullers_
```

##### RTP 数据转发（核心数据路径）

```
远程机器 RTP → UDP → RtcRecvRelay
                            │
                            ▼
              OnRtpPacketFromRemoteRtcPusher()
                            │
本地客户端 RTP → SRTP → MediaPusher
                            │
                            ▼
              OnRtpPacketFromRtcPusher()
                            │
                    ┌───────┴───────┐
                    ▼               ▼
            pusher2pullers_    pusher_user_id2sendRelay_
         （遍历所有 MediaPuller）    （RtcSendRelay → UDP → 远程）
                    │
                    ▼
        MediaPuller::OnTransportSendRtp()
                    │
        ├── 修改 MID 扩展 ID（适配拉流端）
        ├── 修改 TCC 扩展 ID
        ├── 修改 abs-send-time 扩展 ID
        ├── 修改 payload_type（适配拉流端编解码器）
        ├── RtpSendSession::SendRtpPacket()（序列号分配）
        └── SRTP 加密 → UDP 发送给拉流客户端
```

### 3. WebRtcSession — WebRTC 连接会话

- **方向类型**：
  - `SRTP_SESSION_TYPE_RECV`：接收方向（推流方连接，作为 server）
  - `SRTP_SESSION_TYPE_SEND`：发送方向（拉流方连接，作为 server）

- **连接建立过程**：
  1. `IceServer` 处理 STUN 绑定请求，解析 Remote Candidate
  2. `DtlsSession` DTLS-SRTP 握手，协商加密套件
  3. 握手完成后派生 SRTP 密钥，创建 `SRtpSession`

- **成员映射**：
  - `ssrc2media_pusher_` / `mid2media_pusher_`：推流者 SSRC/MID 映射
  - `ssrc2media_puller_` / `rtxssrc2media_puller_`：拉流者 SSRC 映射

- **RTCP 处理**：
  - `HandleRtcpSrPacket()` — Sender Report
  - `HandleRtcpRrPacket()` — Receiver Report
  - `HandleRtcpXrPacket()` — Extended Report
  - `HandleRtcpRtpfbPacket()` — RTP Feedback（NACK、TCC）
  - `HandleRtcpPsfbPacket()` — PS Feedback（PLI关键帧请求）

### 4. MediaPusher — 推流者（接收端）

- 管理 `ssrc → RtpRecvSession` 和 `rtx_ssrc → RtpRecvSession` 的映射
- `HandleRtpPacket()`：
  1. 设置 RTP 扩展 ID（mid/tcc/abs_send_time）
  2. 解析 SSRC → 查找或动态创建 `RtpRecvSession`
  3. `RtpRecvSession::ReceiveRtpPacket()` 序列号检测 + Jitter 计算 + NACK
  4. 如果是音频且启用了 VoiceAgent，clone 包推送给 `VoiceAgent`
  5. 回调 `Room::OnRtpPacketFromRtcPusher()` 进入转发
- `HandleRtcpSrPacket()` — 处理 SR，记录 NTP 时间戳用于后续 DLSR 计算
- 周期统计（每 5 秒输出 Recv kbps/pps）
- 关键帧请求：每 8 秒发送 PLI（`RtcpPsPli`）
- 支持动态 SSRC 更新（`UpdateSSRC()` — WebRTC 重协商时 SSRC 可能变化）

### 5. MediaPuller — 拉流者（发送端）

- 包含 `RtpSendSession`，负责将推流者的 RTP 包转发给拉流客户端
- `OnTransportSendRtp()`：
  1. 检查连接状态
  2. 修改 RTP 扩展头 ID（mid/tcc/abs_send_time）匹配拉流端 SDP 协商值
  3. `RtpSendSession::SendRtpPacket()` 分配序列号
  4. 修改 payload_type 为拉流端协商的值
  5. 通过 SRTP 加密后发送
  6. 恢复原始 payload_type（包可能被多个拉流者共享）
- `HandleRtcpRrBlock()` / `HandleRtcpFbNack()` — 处理拉流端的 RR/NACK

### 6. RtpRecvSession — RTP 接收会话

继承 `RtpSession`（序列号管理）+ `NackGeneratorCallbackI`

- `ReceiveRtpPacket()` — 初始化/更新序列号（`UpdateSeq`）、Wraparound 处理、Jitter 计算
- `ReceiveRtxPacket()` — RTX 解复用（`RtxDemux`）、NACK 列检查去重、恢复为正常 RTP
- `GenerateJitter()` — 基于到达时间差与 RTP 时间戳差的抖动计算（Q4 定点数）
- `SendRtcpRR()` — 定期发送 Receiver Report
  - 视频：每 400ms
  - 音频：每 2000ms
  - RR 包含：cumulative lost、fraction lost、highest seq、jitter、LSR/DLSR
- `HandleRtcpSrPacket()` — 记录 NTP/LSR/RTP 时间戳用于 DLSR 计算
- `GetLostStatics()` — 期望包数 vs 实际接收 → 丢包统计
- NACK 生成：`NackGenerator` 检测缺失序列号 → `GenerateNackList()` → 发送 RTCP NACK

### 7. RtpSendSession — RTP 发送会话

- `SendRtpPacket()` — 序列号分配，存储 RTX 缓存
- `RecvRtcpFbNack()` — 解析 NACK，触发 `RetransmitRtxPackets()` RTX 重传
- `RecvRtcpRrBlock()` — 解析 RR，计算 RTT 和丢包率
- `StoreRtxPacket()` — RTX 包缓存（默认 30ms RTT 估算）
- 周期发送 RTCP SR

### 8. 远程 Relay 组件

#### RtcRecvRelay — 接收远程流

- 开启 UDP 监听端口，接收远端机器推流过来的 RTP 包
- 包含 `RtpRecvSession` 处理序列号和丢包
- `HandleRtpPacket()` → 回调 `Room::OnRtpPacketFromRemoteRtcPusher()`
- `RequestKeyFrame()` → 发送 PLI 给远端
- 支持 `DiscardPacketByPercent()` 丢包模拟

#### RtcSendRelay — 发送到远程

- 通过 UDP 将本地 RTP 流发送给远端机器
- 包含 `RtpSendSession` 管理发送侧
- `SendRtpPacket()` → `RtpSendSession::SendRtpPacket()` → UDP 直发
- 处理来自远端的 RTCP 反馈

---

## 辅助组件

| 组件 | 功能 |
|------|------|
| `IceServer` | STUN 绑定请求处理，生成 ice-ufrag/pwd |
| `DtlsSession` | DTLS-SRTP 握手，证书指纹生成与验证，SRTP 密钥派生 |
| `SRtpSession` | 基于 libsrtp2 的 RTP/RTCP 加密解密 |
| `NackGenerator` | NACK 列表维护、序列号间隙检测、超时重传、RTT 自适应 |
| `TccServer` | Transport-wide CC 反馈包生成 |
| `FastJitterBuffer` | 用于 VoiceAgent 的音频包重排序缓冲 |
| `PilotMessageClient` | 通过 WebSocket Protoo 协议与 PilotCenter 通信 |
| `Whip` | WHIP（WebRTC-HTTP Ingestion Protocol）HTTP REST 推流入网 |
| `RtcUser` | 用户信息（id/name/audience/remote/whip/heartbeat/pushers） |
| `RtpSession` | 基类，序列号管理（InitSeq/UpdateSeq/cycles_/wraparound） |

---

## VoiceAgent 语音代理集成

```
客户端 RTP 音频包
       │
       ▼
MediaPusher::HandleRtpPacket()
       │ (检测 audio + voice_agent_cfg_.enable_)
       ├── Clone RTP 包
       ▼
VoiceAgent::PushRtpPacket()
       │
       ▼
FastJitterBuffer（排序去抖）
       │
       ▼
VoiceAgent::OnJitterBufferRtpPacket()
       │ (组装音频帧)
       ▼
通过 WebSocket Protoo → AI 服务器
       │
       ├── 语音识别结果 → OnVoiceAgentRecognizedText() → Room → PilotCenter
       ├── AI 回复文本   → OnVoiceAgentResponseText()  → Room → PilotCenter
       └── AI 语音（Opus）→ OnVoiceAgentAiOpusData()
               │
               ▼
         Room::GenRtpPacketFromOpusData()
               │ (封装为 RTP 包)
               ▼
         注入 pusher2pullers_ 分发给房间内所有拉流者
```

---

## 数据流总览

```
推流端（客户端A）                服务器                    拉流端（客户端B）
     │                            │                            │
     ├── offer SDP ─────────────►│                            │
     │◄── answer SDP ────────────┤                            │
     │                            │                            │
     ├── STUN/DTLS/SRTP ─────────┤                            │
     │                            │                            │
     ├── RTP + RTCP(SR) ────────►│                            │
     │                      MediaPusher                        │
     │                    (RtpRecvSession)                     │
     │                            │                            │
     │                            ├── offer SDP ◄──────────────┤
     │                            ├──► answer SDP ─────────────┤
     │                            │                            │
     │                            ├── STUN/DTLS/SRTP ──────────┤
     │                            │                            │
     │                    pusher2pullers_[pusher_id]           │
     │                            │                            │
     │                   MediaPuller.OnTransportSendRtp()      │
     │                  (修改 mid/tcc/payload_type)            │
     │                   RtpSendSession.SendRtpPacket()        │
     │                            │                            │
     │                            ├── RTP + RTCP(SR) ─────────►│
     │                            │◄── RTCP(RR/NACK/PLI) ──────┤
     │◄── RTCP(PLI/NACK) ────────┤                            │
```

### 跨机器远程流

```
机器A（推流）        机器B（SFU）             机器C（拉流）
                      ┌─────────┐
      UDP ──────────► │RtcRecv  │───────────────────────►
                      │ Relay   │  OnRtpPacketFromRemote
                      └─────────┘  RtcPusher()
                           │
                      pusher2pullers_
                           │
                      MediaPuller
                           │
                      SRTP/UDP ────────────────────────► 客户端C
                           │
                      ┌─────────┐
                      │RtcSend  │── UDP ──────────────► 机器D
                      │ Relay   │
                      └─────────┘
```

---

## 心跳与生命周期

- **客户端 WebSocket 心跳**：`Room::HandleWsHeartbeat()` 更新 `RtcUser::last_heartbeat_ms_`
- **Room 定时器（10ms）**：
  - 检测 `RtcUser::IsAlive()` — 超时则 `ReleaseUserResources()`
  - 检测 `RtcRecvRelay::IsAlive()` — 超时则清理
  - 执行 VoiceAgent RTP 发送
- **RoomMgr 定时器（1000ms）**：
  - 向 PilotCenter 发送 echo 心跳（5 秒间隔）
  - 检测 `Room::IsAlive()` — 不活跃则移除房间
