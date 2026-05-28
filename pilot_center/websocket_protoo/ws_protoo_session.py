#!/usr/bin/env python3
"""
WebSocket Protoo Session

Implements a per-connection session that speaks a minimal subset of the
"protoo"-like JSON protocol described in ws_design:

Message types (all JSON objects):

- request:
  {
    "request": true,
    "id": 12345678,
    "method": "chatmessage",
    "data": {"type": "text", "value": "Hi there!"}
  }

- successful response:
  {
    "response": true,
    "id": 12345678,
    "ok": true,
    "data": {"foo": "lalala"}
  }

- error response:
  {
    "response": true,
    "id": 12345678,
    "ok": false,
    "errorCode": 123,
    "errorReason": "Something failed"
  }

- notification:
  {
    "notification": true,
    "method": "chatmessage",
    "data": {"foo": "bar"}
  }
"""
from __future__ import annotations

import asyncio
import json
import logging
import random
from typing import Any, Dict, Optional, TYPE_CHECKING

import websockets

if TYPE_CHECKING:
    from ..msu.msg_mgr import MsuManager
    from ..sfu.sfu_mgr import SfuManager
    # Imported only for type checking to avoid circular import at runtime
    from .ws_protoo_server import WsProtooServer


class WsProtooSession:
    """A single WebSocket connection speaking protoo-style JSON messages."""

    def __init__(self, websocket: websockets.WebSocketServerProtocol, server: "WsProtooServer", peer: str,
                 logger: Optional[logging.Logger] = None,
                 room_manager: object | None = None,
                 msu_manager: object | None = None) -> None:
        self.websocket = websocket
        self.server = server
        self.peer = peer
        self.log = logger or logging.getLogger("ws_protoo_session")
        self._recv_task: Optional[asyncio.Task[None]] = None
        self._closed = asyncio.Event()
        # track participations: room_id -> set of user_ids this session represents
        # a single session may join multiple rooms and may represent different
        # user ids in each room (e.g., SFU scenarios)
        self.participations: Dict[str, set[str]] = {}
        # Track pending requests: req_id -> asyncio.Future for response
        self._pending_requests: Dict[int, asyncio.Future] = {}
        self.room_manager = room_manager
        self.msu_manager = msu_manager
        self.sfu_manager: "SfuManager | None" = None
        # Log the remote peer address so operators can see which endpoint connected.
        # Message kept concise and consistent with other session logs.
        try:
            self.log.info("New protoo session connected from %s", self.peer)
        except Exception:
            # Logging must not throw during construction
            pass

        

    async def run(self) -> None:
        """Enter receive loop until connection is closed."""
        self.log.info("Session started: %s", self.peer)
        try:
            async for raw in self.websocket:
                await self._on_message(raw)
        except websockets.ConnectionClosedOK:
            self.log.info("Connection closed (OK): %s", self.peer)
        except websockets.ConnectionClosedError as e:
            self.log.warning("Connection closed (error) %s: %s", self.peer, e)
        except Exception as e:
            self.log.exception("Unhandled error in session %s: %s", self.peer, e)
        finally:
            await self.close()

    async def close(self) -> None:
        if not self._closed.is_set():
            self._closed.set()
            try:
                await self.websocket.close()
            except Exception:
                pass
            # unregister at server and room manager if present
            try:
                self.server.unregister(self)
            except Exception:
                pass
            rm = getattr(self.server, "room_manager", None)
            if rm is not None:
                # remove all participations (room_id -> set(user_id))
                try:
                        for room_id, user_ids in list(self.participations.items()):
                            for uid in list(user_ids):
                                try:
                                    # disassociate session from the User object
                                    user = rm.get_user(room_id, uid)
                                    if user is not None:
                                        try:
                                            user.remove_session(self)
                                        except Exception:
                                            pass
                                    # remove session entry from Room's session map
                                    try:
                                        sid = getattr(self, "peer", None)
                                        if sid:
                                            room = rm.get_or_create_room(room_id)
                                            try:
                                                room.remove_session(sid)
                                            except Exception:
                                                pass
                                    except Exception:
                                        pass
                                    # if user has no sessions left, remove the user from the room
                                    try:
                                        if user is None or not user.has_sessions():
                                            rm.remove_user_from_room(room_id, uid)
                                    except Exception:
                                        pass
                                except Exception:
                                    pass
                except Exception:
                    pass
            # clean up SFU registration if this session is an SFU service
            sfu_mgr = getattr(self.server, "sfu_manager", None)
            if sfu_mgr is not None:
                try:
                    sfu_mgr.remove_by_session(self)
                except Exception:
                    pass
            self.log.info("Session closed: %s", self.peer)

    async def _on_message(self, raw: Any) -> None:
        # websockets yields str for text frames by default
        if not isinstance(raw, str):
            self.log.debug("Ignoring non-text frame from %s", self.peer)
            return
        try:
            msg = json.loads(raw)
        except json.JSONDecodeError:
            self.log.debug("Invalid JSON from %s: %r", self.peer, raw[:200])
            return
        if not isinstance(msg, dict):
            self.log.debug("Ignoring non-object JSON from %s", self.peer)
            return

        # Dispatch by top-level flags
        if msg.get("request") is True:
            await self._handle_request(msg)
        elif msg.get("response") is True:
            await self._handle_response(msg)
        elif msg.get("notification") is True:
            await self._handle_notification(msg)
        else:
            self.log.debug("Unknown message shape from %s: %s", self.peer, msg)

    async def _handle_request(self, msg: Dict[str, Any]) -> None:
        req_id = msg.get("id")
        method = msg.get("method")
        data = msg.get("data")

        if not isinstance(req_id, (int, str)):
            await self.send_response_error(req_id, 400, "Invalid id")
            return
        if not isinstance(method, str):
            await self.send_response_error(req_id, 400, "Invalid method")
            return

        self.log.info("Received request from %s: method=%s, data=%s", self.peer, method, data)
        try:
            if method == "echo":
                # SFU service heartbeat/register — create or update SFU instance
                sfu_mgr = getattr(self.server, "sfu_manager", None)
                if sfu_mgr is not None:
                    ws_url = data.get("ws_url") if isinstance(data, dict) else None
                    if not isinstance(ws_url, str) or not ws_url:
                        await self.send_response_error(req_id, 400, "Missing ws_url in echo data")
                        return
                    sfu_mgr.add_or_update(self, ws_url)
                    self.sfu_manager = sfu_mgr
                    self.log.info("SFU registered/updated: %s", ws_url)
                    await self.send_response_ok(req_id, {"registered": True, "ws_url": ws_url})
                else:
                    # No SFU manager configured — fallback to simple echo
                    await self.send_response_ok(req_id, {"echo": data})
            elif method == "register":
                self.log.info(f"register data: {data}")
                msu_id = data.get("id") if isinstance(data, dict) else None
                logging.info("Register request: id=%s", msu_id)
                # validate id
                if not isinstance(msu_id, str) or not msu_id:
                    await self.send_response_error(req_id, 400, "Invalid id")
                    return

                try:
                    item = self.msu_manager.get(msu_id)
                    if item is None:
                        self.log.info("MSU added: %s", msu_id)
                        self.msu_manager.add_or_update(self, msu_id)
                    else:
                        self.log.info("MSU updated: %s", msu_id)
                        self.msu_manager.add_or_update(self, msu_id)
                    await self.send_response_ok(req_id, {"registered": True, "msuId": msu_id})
                except Exception as e:
                    self.log.exception("Failed to register MSU %s: %s", msu_id, e)
                    await self.send_response_error(req_id, 500, "Register failed")

            elif method == "join":
                roomId = data.get("roomId") if isinstance(data, dict) else None
                userId = data.get("userId") if isinstance(data, dict) else None
                userName = data.get("userName") if isinstance(data, dict) else None
                audience = data.get("audience")
                if not isinstance(audience, bool):
                    audience = False

                roomToken = data.get("roomToken", "") if isinstance(data, dict) else ""
                logging.info("User join request: roomId=%s, userId=%s, userName=%s, audience=%s", roomId, userId, userName, audience)
                # integrate with RoomManager if available
                rm = getattr(self.server, "room_manager", None)
                if rm is None:
                    # server not configured with a room manager — still succeed but no registration
                    self.log.debug("No room_manager configured on server; skipping room join bookkeeping")
                    await self.send_response_ok(req_id, {"joined": True})
                    return
                # validate roomToken
                if not rm.validate_room_token(roomId, roomToken):
                    self.log.warning("join: invalid roomToken for roomId=%s", roomId)
                    await self.send_response_error(req_id, 403, "Invalid roomToken")
                    return
                logging.info("join: valid roomToken=%s for roomId=%s", roomToken, roomId)
                # validate inputs
                if not isinstance(roomId, str) or not isinstance(userId, str):
                    await self.send_response_error(req_id, 400, "Invalid roomId or userId")
                    return
                data = rm.handle_join(room_id=roomId, user_id=userId, user_name=userName or "", audience=audience, session=self)
                # record participation locally so close() can cleanup
                self.participations.setdefault(roomId, set()).add(userId)

                try:
                    if not audience:
                        await self.msu_manager.handle_join_room(roomId, userId, userName)
                except Exception as e:
                    self.log.exception("Failed to handle MSU join for room %s: %s", roomId, e)
                self.log.info("join response data: %s", json.dumps(data))
                await self.send_response_ok(req_id, data)
            else:
                await self.send_response_error(req_id, 404, f"Unknown method: {method}")
        except Exception as e:
            self.log.exception("Error handling request %s: %s", method, e)
            await self.send_response_error(req_id, 500, "Internal error")

    async def send_response_ok(self, req_id: Any, data: Optional[Dict[str, Any]] = None) -> None:
        payload = {"response": True, "id": req_id, "ok": True, "data": data or {}}
        await self._send_json(payload)

    async def send_response_error(self, req_id: Any, code: int, reason: str) -> None:
        payload = {
            "response": True,
            "id": req_id,
            "ok": False,
            "errorCode": int(code),
            "errorReason": str(reason),
        }
        await self._send_json(payload)

    async def send_notification(self, method: str, data: Optional[Dict[str, Any]] = None) -> None:
        payload = {"notification": True, "method": method, "data": data or {}}
        await self._send_json(payload)

    async def _handle_response(self, msg: Dict[str, Any]) -> None:
        """Handle response messages from peer."""
        req_id = msg.get("id")
        if req_id not in self._pending_requests:
            self.log.debug("Received response for unknown request id: %s", req_id)
            return
        
        future = self._pending_requests.get(req_id)
        if future and not future.done():
            if msg.get("ok") is True:
                future.set_result(msg.get("data", {}))
            else:
                error_code = msg.get("errorCode", 0)
                error_reason = msg.get("errorReason", "Unknown error")
                future.set_exception(Exception(f"Request failed: [{error_code}] {error_reason}"))

    async def send_request(self, method: str, data: Optional[Dict[str, Any]] = None, timeout: float = 10.0) -> Dict[str, Any]:
        """Send a request to the peer and wait for response."""
        req_id = random.randint(10000000, 99999999)
        payload = {"request": True, "id": req_id, "method": method, "data": data or {}}
        
        # Create future to wait for response
        future: asyncio.Future = asyncio.Future()
        self._pending_requests[req_id] = future
        
        try:
            await self._send_json(payload)
            # Wait for response with timeout
            result = await asyncio.wait_for(future, timeout=timeout)
            return result
        except asyncio.TimeoutError:
            raise TimeoutError(f"Request {method} (id={req_id}) timed out after {timeout}s")
        finally:
            # Clean up pending request
            self._pending_requests.pop(req_id, None)

    async def _handle_notification(self, msg: Dict[str, Any]) -> None:
        """Process client-sent notifications.

        Current behavior:
        - Log the notification with method and peer.
        - Optionally rebroadcast to other peers (disabled by default).
        """
        method = msg.get("method")
        data = msg.get("data")
        if not isinstance(method, str):
            self.log.debug("Invalid notification method from %s: %r", self.peer, method)
            return
        # Log notification
        self.log.info("Client notification from %s method:%s, data:%s", self.peer, method, data)
        if method == "push":
            # call room manager to handle push notification
            rm = getattr(self.server, "room_manager", None)
            if rm is not None:
                rm.handle_push_notification(data, self)
        elif method == "pullRemoteStream":
            rm = getattr(self.server, "room_manager", None)
            if rm is not None:
                rm.handle_pull_remote_stream_notification(data, self)
        elif method == "userDisconnect":
            rm = getattr(self.server, "room_manager", None)
            if rm is not None:
                rm.handle_userDisconnect_notification(data, self)
        elif method == "userLeave":
            rm = getattr(self.server, "room_manager", None)
            if rm is not None:
                rm.handle_userLeave_notification(data, self)
        elif method == "textMessage":
            rm = getattr(self.server, "room_manager", None)
            if rm is not None:
                rm.handle_textMessage_notification(data, self)
        elif method == "asrResult":
            rm = getattr(self.server, "room_manager", None)
            if rm is not None:
                rm.handle_asr_result_notification(data, self)
        else:
            self.log.error("Unhandled notification method from %s: %s", self.peer, method)
    async def _send_json(self, obj: Dict[str, Any]) -> None:
        try:
            await self.websocket.send(json.dumps(obj))
        except Exception as e:
            self.log.debug("Send failed to %s: %s", self.peer, e)
