English | [中文](README_cn.md)
# WebRTC JS Client (Vue 3)

Vue 3 + Vite + TypeScript WebRTC front-end sample. It wraps publish/subscribe flows, room signaling, and basic text chat, serving as a starter or debugging client.

## Features
- WebSocket signaling via `protoo-client` (default URL is configurable in App).
- `PCWrap` wraps `RTCPeerConnection` with sendonly/recvonly/sendrecv modes; handles device opening, offer creation, and track management for pulls.
- Room/user lifecycle: join/leave, pushers list, disconnect/reconnect markers.
- Text chat and simple UI controls (mute audio, hide remote video, etc.).

Entry point: `src/main.ts` mounts `src/App.vue`; core logic lives in `src/peerConnectionWrap/` and `src/App.vue`.

## Prerequisites
- Node.js ≥ 18 (required by Vite 7).
- npm installed.

## Quick Start
1) Install dependencies

	```bash
	npm install
	```

2) Start dev server (default http://localhost:5173/)

	```bash
	npm run dev
	```

3) Build for production

	```bash
	npm run build
	```

4) Preview the build output

	```bash
	npm run preview
	```

## Project Layout
- `src/App.vue`: room logic, signaling, chat, and remote stream management.
- `src/peerConnectionWrap/PCWrap.ts`: lightweight RTCPeerConnection helper.
- `src/peerConnectionWrap/ProtooClientWrap.ts`: signaling client wrapper.
- `src/style.css`: global styles.

## Server Side
- Open-source signaling/media server: [https://github.com/runner365/RTCPilot](https://github.com/runner365/RTCPilot)

## Config & Debugging
- Default signaling URL is the `wsUrl` constant in `src/App.vue`; adjust to your server.
- To customize ICE servers or media constraints, pass `iceServers` and `mediaConstraints` when constructing `PCWrap`.

## Troubleshooting
- Cannot get camera/mic: check browser permissions or https origin.
- Publish/subscribe fails: confirm signaling URL, room ID, and user ID/name match the server configuration.
=======
# Vue 3 + TypeScript + Vite

This template should help get you started developing with Vue 3 and TypeScript in Vite. The template uses Vue 3 `<script setup>` SFCs, check out the [script setup docs](https://v3.vuejs.org/api/sfc-script-setup.html#sfc-script-setup) to learn more.

Learn more about the recommended Project Setup and IDE Support in the [Vue Docs TypeScript Guide](https://vuejs.org/guide/typescript/overview.html#project-setup).

