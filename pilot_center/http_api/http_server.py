#!/usr/bin/env python3
"""Minimal async HTTP server for the Pilot Center REST API."""
from __future__ import annotations

import asyncio
import json
import logging
import random
import secrets
from datetime import datetime, timezone
from typing import Optional

from eth_account.messages import encode_defunct
from eth_account import Account

from database.db import Database, generate_nonce, normalize_address

HTTP_200 = "200 OK"
HTTP_400 = "400 Bad Request"
HTTP_401 = "401 Unauthorized"
HTTP_403 = "403 Forbidden"
HTTP_404 = "404 Not Found"
HTTP_405 = "405 Method Not Allowed"

def _json_bytes(obj: object) -> bytes:
    return json.dumps(obj, ensure_ascii=False).encode("utf-8")


def _cors_headers(origin: str = "*") -> str:
    return (
        f"Access-Control-Allow-Origin: {origin}\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
    )


def verify_personal_sign(message: str, signature: str) -> str | None:
    """Verify an EIP-191 personal_sign signature. Returns recovered address (lowercase) or None."""
    try:
        signable = encode_defunct(text=message)
        recovered = Account.recover_message(signable, signature=signature)
        return recovered.lower()
    except Exception:
        return None


def _generate_room_id() -> str:
    return secrets.token_hex(12)


def _generate_room_token() -> str:
    return secrets.token_hex(32)


class HttpApiServer:
    """Serves REST endpoints on the given host:port.

    Endpoints:
      GET  /api/v1/contracts      → contract addresses
      POST /api/v1/auth/challenge → generate login challenge message
      POST /api/v1/auth/login     → verify signature and authenticate
      POST /api/v1/createroom     → verify on-chain meeting and create WebRTC room
    """

    def __init__(self, host: str, port: int, contracts: dict[str, str],
                 sfu_mgr: object | None = None,
                 room_mgr: object | None = None,
                 rpc_client: object | None = None,
                 logger: Optional[logging.Logger] = None,
                 db: Optional[Database] = None) -> None:
        self.host = host
        self.port = port
        self.contracts = contracts
        self.sfu_mgr = sfu_mgr
        self.room_mgr = room_mgr
        self.rpc_client = rpc_client
        self.meeting_manager_addr = contracts.get("meeting_manager", "")
        self.meeting_token_addr = contracts.get("meeting_token", "")
        self.log = logger or logging.getLogger("http_api")
        self._server: Optional[asyncio.AbstractServer] = None
        self._db = db

    async def start(self) -> None:
        self._server = await asyncio.start_server(
            self._handle_connection, self.host, self.port)
        self.log.info("HTTP API server listening on %s:%d", self.host, self.port)

    async def stop(self) -> None:
        if self._server is not None:
            self._server.close()
            await self._server.wait_closed()
            self._server = None
        self.log.info("HTTP API server stopped")

    # ------------------------------------------------------------------
    # Auth helper
    # ------------------------------------------------------------------

    async def _verify_auth(self, headers_text: str) -> str | None:
        """Extract Bearer token from Authorization header, verify, return address."""
        token: str | None = None
        for line in headers_text.split("\r\n"):
            if line.lower().startswith("authorization:"):
                value = line.split(":", 1)[1].strip()
                if value.lower().startswith("bearer "):
                    token = value[7:].strip()
                    break

        if token is None or self._db is None:
            return None
        return await self._db.verify_token(token)

    # ------------------------------------------------------------------
    # HTTP parsing helpers
    # ------------------------------------------------------------------

    async def _read_body(self, reader: asyncio.StreamReader, headers_text: str) -> bytes:
        for line in headers_text.split("\r\n"):
            if line.lower().startswith("content-length:"):
                try:
                    length = int(line.split(":", 1)[1].strip())
                except ValueError:
                    return b""
                return await reader.readexactly(length)
        return b""

    # ------------------------------------------------------------------
    # Route dispatcher
    # ------------------------------------------------------------------

    async def _handle_connection(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        try:
            raw = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), timeout=10)
        except (asyncio.TimeoutError, asyncio.IncompleteReadError):
            writer.close()
            return

        request = raw.decode("utf-8", errors="replace")
        lines = request.split("\r\n")
        head = lines[0]
        parts = head.split(" ")
        if len(parts) < 2:
            writer.close()
            return

        method = parts[0].upper()
        path = parts[1]

        self.log.info("HTTP %s %s", method, path)

        if method == "OPTIONS":
            await self._send(writer, HTTP_200, b"")
            return

        if path == "/api/v1/contracts":
            await self._handle_contracts(method, writer)
        elif path == "/api/v1/auth/challenge":
            await self._handle_auth_challenge(method, reader, writer, request)
        elif path == "/api/v1/auth/login":
            await self._handle_auth_login(method, reader, writer, request)
        elif path == "/api/v1/createroom":
            await self._handle_createroom(method, reader, writer, request)
        elif path == "/api/v1/joinroom":
            await self._handle_joinroom(method, reader, writer, request)
        elif path == "/api/v1/user/token-balance":
            await self._handle_token_balance(method, writer, request)
        else:
            self.log.warning("HTTP 404 %s %s", method, path)
            await self._send(writer, HTTP_404, b'{"error":"not found"}')

    # ------------------------------------------------------------------
    # GET /api/v1/contracts
    # ------------------------------------------------------------------

    async def _handle_contracts(self, method: str, writer: asyncio.StreamWriter) -> None:
        if method != "GET":
            self.log.warning("contracts: method not allowed: %s", method)
            await self._send(writer, HTTP_405, b'{"error":"method not allowed"}')
            return
        await self._send(writer, HTTP_200, _json_bytes(self.contracts))

    # ------------------------------------------------------------------
    # POST /api/v1/auth/challenge
    # ------------------------------------------------------------------

    async def _handle_auth_challenge(self, method: str, reader: asyncio.StreamReader,
                                     writer: asyncio.StreamWriter, headers_text: str) -> None:
        if method != "POST":
            self.log.warning("auth/challenge: method not allowed: %s", method)
            await self._send(writer, HTTP_405, b'{"error":"method not allowed"}')
            return
        if self._db is None:
            self.log.error("auth/challenge: database not configured")
            await self._send(writer, HTTP_200, b'{"code":-1,"error":"database not configured"}')
            return

        payload = await self._parse_json_body(reader, headers_text)
        if payload is None:
            self.log.warning("auth/challenge: invalid json body")
            await self._send(writer, HTTP_400, b'{"code":-1,"error":"invalid json body"}')
            return

        address = payload.get("address", "")
        if not address:
            self.log.warning("auth/challenge: missing address field")
            await self._send(writer, HTTP_400, b'{"code":-1,"error":"missing address field"}')
            return

        self.log.info("auth/challenge: address=%s", address)

        try:
            user = await self._db.get_or_create_user(address)
        except ValueError:
            self.log.warning("auth/challenge: invalid ethereum address: %s", address)
            await self._send(writer, HTTP_400, b'{"code":-1,"error":"invalid ethereum address"}')
            return

        new_nonce = generate_nonce()
        await self._db.update_user_nonce(user.address, new_nonce)

        message = (
            "Welcome to RTCPilot!\n\n"
            "Please sign this message to authenticate.\n\n"
            f"Address: {user.address}\n"
            f"Nonce: {new_nonce}"
        )

        self.log.info("auth/challenge: challenge generated for %s", address)
        resp = {"code": 0, "message": message}
        await self._send(writer, HTTP_200, _json_bytes(resp))

    # ------------------------------------------------------------------
    # POST /api/v1/auth/login
    # ------------------------------------------------------------------

    async def _handle_auth_login(self, method: str, reader: asyncio.StreamReader,
                                  writer: asyncio.StreamWriter, headers_text: str) -> None:
        if method != "POST":
            self.log.warning("auth/login: method not allowed: %s", method)
            await self._send(writer, HTTP_405, b'{"error":"method not allowed"}')
            return
        if self._db is None:
            self.log.error("auth/login: database not configured")
            await self._send(writer, HTTP_200, b'{"code":-1,"error":"database not configured"}')
            return

        payload = await self._parse_json_body(reader, headers_text)
        if payload is None:
            self.log.warning("auth/login: invalid json body")
            await self._send(writer, HTTP_400, b'{"code":-1,"error":"invalid json body"}')
            return

        address = payload.get("address", "")
        message = payload.get("message", "")
        signature = payload.get("signature", "")

        if not address or not message or not signature:
            self.log.warning("auth/login: missing fields address=%s", address)
            await self._send(writer, HTTP_400,
                             b'{"code":-1,"error":"missing address, message, or signature"}')
            return

        self.log.info("auth/login: verifying signature for %s", address)

        recovered = verify_personal_sign(message, signature)
        if recovered is None:
            self.log.warning("auth/login: signature verification failed for %s", address)
            await self._send(writer, HTTP_401,
                             b'{"code":-1,"error":"signature verification failed"}')
            return

        if normalize_address(recovered) != normalize_address(address):
            self.log.warning("auth/login: address mismatch recovered=%s requested=%s", recovered, address)
            await self._send(writer, HTTP_401,
                             b'{"code":-1,"error":"signature does not match address"}')
            return

        try:
            user = await self._db.login_user(address)
        except ValueError:
            self.log.warning("auth/login: user not found: %s", address)
            await self._send(writer, HTTP_401,
                             b'{"code":-1,"error":"user not found"}')
            return

        token = await self._db.create_session(address)

        
        resp = {
            "code": 0,
            "user": {
                "address": user.address,
                "created_at": user.created_at.isoformat(),
                "last_login_at": user.last_login_at.isoformat(),
            },
            "token": token,
        }
        await self._send(writer, HTTP_200, _json_bytes(resp))

    # ------------------------------------------------------------------
    # POST /api/v1/createroom
    # ------------------------------------------------------------------

    async def _handle_createroom(self, method: str, reader: asyncio.StreamReader,
                                  writer: asyncio.StreamWriter, headers_text: str) -> None:
        if method != "POST":
            self.log.warning("createroom: method not allowed: %s", method)
            await self._send(writer, HTTP_405, b'{"error":"method not allowed"}')
            return
        if self._db is None:
            self.log.error("createroom: database not configured")
            await self._send(writer, HTTP_200,
                             b'{"code":-1,"error":"database not configured"}')
            return

        user_address = await self._verify_auth(headers_text)
        if user_address is None:
            self.log.warning("createroom: authentication required")
            await self._send(writer, HTTP_401,
                             b'{"code":-1,"error":"authentication required"}')
            return

        payload = await self._parse_json_body(reader, headers_text)
        meeting_id = 0
        if isinstance(payload, dict):
            try:
                meeting_id = int(payload.get("meetingId", 0))
            except (TypeError, ValueError):
                pass

        if meeting_id <= 0:
            self.log.warning("createroom: missing or invalid meetingId")
            await self._send(writer, HTTP_400,
                             b'{"code":-1,"error":"missing or invalid meetingId"}')
            return

        block_number = ""
        if isinstance(payload, dict):
            block_number = str(payload.get("blockNumber", "") or "")

        # Validate meeting on-chain
        if self.rpc_client is not None and self.meeting_manager_addr:
            block_param = block_number if block_number else "latest"
            try:
                self.log.info("createroom: validating meetingId=%d on-chain block=%s",
                              meeting_id, block_param)
                meeting = await self.rpc_client.get_meeting(
                    self.meeting_manager_addr, meeting_id, block=block_param)
            except Exception as e:
                self.log.error("createroom: RPC getMeeting failed: %s", e)
                await self._send(writer, HTTP_200,
                                 b'{"code":-1,"error":"failed to verify meeting on-chain"}')
                return

            self.log.info("createroom: on-chain meeting data: %s", json.dumps(meeting, ensure_ascii=False))
            if meeting is None:
                self.log.warning("createroom: meetingId=%d not found on-chain", meeting_id)
                await self._send(writer, HTTP_200,
                                 b'{"code":-1,"error":"meeting not found on-chain"}')
                return

            if not meeting.get("active"):
                self.log.warning("createroom: meetingId=%d is not active", meeting_id)
                await self._send(writer, HTTP_200,
                                 b'{"code":-1,"error":"meeting is not active"}')
                return

            meeting_creator = meeting.get("creator", "").lower()
            if meeting_creator != user_address.lower():
                self.log.warning("createroom: meeting creator mismatch, expected=%s got=%s",
                                 meeting_creator, user_address)
                await self._send(writer, HTTP_403,
                                 b'{"code":-1,"error":"meeting creator does not match user"}')
                return

            self.log.info("createroom: meetingId=%d validated on-chain, creator=%s",
                          meeting_id, meeting_creator)
        else:
            self.log.warning("createroom: RPC client not configured, skipping on-chain validation")

        room_id = _generate_room_id()
        room_token = _generate_room_token()
        created_at = datetime.now(timezone.utc)

        self.log.info("createroom: creating roomId=%s for meetingId=%d user=%s, token=%s",
                      room_id, meeting_id, user_address, room_token)
        # Persist room-meeting mapping
        try:
            ok = await self._db.create_room_meeting(meeting_id, room_id, user_address)
            if not ok:
                self.log.warning("createroom: meetingId=%d already linked to a room", meeting_id)
                await self._send(writer, HTTP_200,
                                 b'{"code":-1,"error":"meeting already has a room"}')
                return
        except Exception as e:
            self.log.error("createroom: DB write failed: %s", e)

        if self.room_mgr is not None:
            self.room_mgr.register_room_token(room_id, room_token)

        # select a random SFU ws_url from the pool
        ws_url = ""
        if self.sfu_mgr is not None:
            urls = list(self.sfu_mgr.list_ws_urls())
            if urls:
                ws_url = random.choice(urls)
                self.log.info("createroom: selected sfu ws_url=%s", ws_url)
            else:
                self.log.warning("createroom: no sfu available in pool")
        else:
            self.log.warning("createroom: sfu_mgr not configured")

        self.log.info("createroom: user=%s meetingId=%d roomId=%s wsUrl=%s",
                      user_address, meeting_id, room_id, ws_url)
        resp = {
            "code": 0,
            "roomId": room_id,
            "roomToken": room_token,
            "wsUrl": ws_url,
            "createdAt": created_at.isoformat(),
        }

        await self._send(writer, HTTP_200, _json_bytes(resp))

    # ------------------------------------------------------------------
    # POST /api/v1/joinroom
    # ------------------------------------------------------------------

    async def _handle_joinroom(self, method: str, reader: asyncio.StreamReader,
                               writer: asyncio.StreamWriter, headers_text: str) -> None:
        if method != "POST":
            self.log.warning("joinroom: method not allowed: %s", method)
            await self._send(writer, HTTP_405, b'{"error":"method not allowed"}')
            return
        if self._db is None:
            self.log.error("joinroom: database not configured")
            await self._send(writer, HTTP_200,
                             b'{"code":-1,"error":"database not configured"}')
            return

        user_address = await self._verify_auth(headers_text)
        if user_address is None:
            self.log.warning("joinroom: authentication required")
            await self._send(writer, HTTP_401,
                             b'{"code":-1,"error":"authentication required"}')
            return

        payload = await self._parse_json_body(reader, headers_text)
        if payload is None:
            self.log.warning("joinroom: invalid json body")
            await self._send(writer, HTTP_400, b'{"code":-1,"error":"invalid json body"}')
            return

        meeting_id = 0
        if isinstance(payload, dict):
            try:
                meeting_id = int(payload.get("meetingId", 0))
            except (TypeError, ValueError):
                pass

        if meeting_id <= 0:
            self.log.warning("joinroom: missing or invalid meetingId")
            await self._send(writer, HTTP_400,
                             b'{"code":-1,"error":"missing or invalid meetingId"}')
            return

        # Validate user has paid (hasJoined) on-chain
        if self.rpc_client is not None and self.meeting_manager_addr:
            try:
                joined = await self.rpc_client.has_joined(
                    self.meeting_manager_addr, meeting_id, user_address)
            except Exception as e:
                self.log.error("joinroom: RPC hasJoined failed: %s", e)
                await self._send(writer, HTTP_200,
                                 b'{"code":-1,"error":"failed to verify join on-chain"}')
                return

            if not joined:
                self.log.warning("joinroom: user %s has not joined meetingId=%d on-chain",
                                 user_address, meeting_id)
                await self._send(writer, HTTP_200,
                                 b'{"code":-1,"error":"user has not paid for this meeting"}')
                return
        else:
            self.log.warning("joinroom: RPC client not configured, skipping payment check")

        # Look up room_id from DB
        room_id = None
        if self._db is not None:
            room_id = await self._db.get_room_by_meeting(meeting_id)

        if not room_id:
            self.log.warning("joinroom: no room found for meetingId=%d", meeting_id)
            await self._send(writer, HTTP_200,
                             b'{"code":-1,"error":"no room found for this meeting"}')
            return

        room_token = _generate_room_token()

        if self.room_mgr is not None:
            self.room_mgr.register_room_token(room_id, room_token)

        ws_url = ""
        if self.sfu_mgr is not None:
            urls = list(self.sfu_mgr.list_ws_urls())
            if urls:
                ws_url = random.choice(urls)
                self.log.info("joinroom: selected sfu ws_url=%s", ws_url)

        self.log.info("joinroom: user=%s meetingId=%d roomId=%s",
                      user_address, meeting_id, room_id)
        resp = {
            "code": 0,
            "roomId": room_id,
            "roomToken": room_token,
            "wsUrl": ws_url,
        }
        await self._send(writer, HTTP_200, _json_bytes(resp))

    # ------------------------------------------------------------------
    # GET /api/v1/user/token-balance
    # ------------------------------------------------------------------

    async def _handle_token_balance(self, method: str,
                                     writer: asyncio.StreamWriter,
                                     headers_text: str) -> None:
        if method != "GET":
            self.log.warning("token-balance: method not allowed: %s", method)
            await self._send(writer, HTTP_405, b'{"error":"method not allowed"}')
            return

        user_address = await self._verify_auth(headers_text)
        if user_address is None:
            self.log.warning("token-balance: authentication required")
            await self._send(writer, HTTP_401,
                             b'{"code":-1,"error":"authentication required"}')
            return

        balance = 0
        if self.rpc_client is not None and self.meeting_token_addr:
            try:
                balance = await self.rpc_client.balance_of(
                    self.meeting_token_addr, user_address)
                if self._db is not None:
                    await self._db.update_token_balance(user_address, balance)
            except Exception as e:
                self.log.warning("token-balance: RPC balanceOf failed: %s", e)
                if self._db is not None:
                    balance, _ = await self._db.get_token_balance(user_address)
        elif self._db is not None:
            balance, _ = await self._db.get_token_balance(user_address)

        self.log.info("token-balance: user=%s balance=%d", user_address, balance)
        resp = {"code": 0, "balance": str(balance)}
        await self._send(writer, HTTP_200, _json_bytes(resp))

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    async def _parse_json_body(self, reader: asyncio.StreamReader, headers_text: str) -> dict | None:
        body_bytes = await self._read_body(reader, headers_text)
        try:
            return json.loads(body_bytes.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError) as e:
            self.log.warning("Failed to parse JSON body: %s", e)
            return None

    async def _send(self, writer: asyncio.StreamWriter, status: str, body: bytes) -> None:
        try:
            body_str = body.decode("utf-8")
            self.log.info("HTTP response %s: %s", status, body_str)
        except Exception:
            pass
        cors = _cors_headers()
        resp = (
            f"HTTP/1.1 {status}\r\n"
            f"{cors}"
            f"Content-Type: application/json\r\n"
            f"Content-Length: {len(body)}\r\n"
            f"Connection: close\r\n"
            f"\r\n"
        ).encode("utf-8") + body
        writer.write(resp)
        await writer.drain()
        writer.close()
