"""Lightweight Ethereum JSON-RPC client.

No web3.py dependency — pure HTTP JSON-RPC calls for the few methods we need:
  - eth_call (for balanceOf, getMeeting, creationFee)
"""

from __future__ import annotations

import logging
from typing import Any, Optional

import aiohttp

# Minimal ABIs encoded as selectors — avoids pulling in full ABI encoders.
# keccak256("balanceOf(address)")[:4]  = 0x70a08231
# keccak256("getMeeting(uint256)")[:4]  = 0x0b27e3f4
# keccak256("creationFee()")[:4]        = 0x61342bd1

_BALANCE_OF_SELECTOR = "0x70a08231"   # keccak256("balanceOf(address)")
_GET_MEETING_SELECTOR = "0x2d703f88"  # keccak256("getMeeting(uint256)")
_CREATION_FEE_SELECTOR = "0xdce0b4e4"  # keccak256("creationFee()")
_HAS_JOINED_SELECTOR = "0x68194719"   # keccak256("hasJoined(uint256,address)")


def _encode_balance_of(address: str) -> str:
    """Encode balanceOf(address) — pad address to 32 bytes."""
    clean = address.lower().replace("0x", "")
    return _BALANCE_OF_SELECTOR + "000000000000000000000000" + clean


def _encode_get_meeting(meeting_id: int) -> str:
    """Encode getMeeting(uint256 meetingId)."""
    return _GET_MEETING_SELECTOR + format(meeting_id, "064x")


def _encode_has_joined(meeting_id: int, address: str) -> str:
    """Encode hasJoined(uint256 meetingId, address attendee)."""
    clean = address.lower().replace("0x", "")
    return _HAS_JOINED_SELECTOR + format(meeting_id, "064x") + "000000000000000000000000" + clean


def _encode_creation_fee() -> str:
    return _CREATION_FEE_SELECTOR


def _decode_uint256(hex_str: str) -> int:
    return int(hex_str, 16)


def _decode_get_meeting(hex_str: str) -> dict[str, Any] | None:
    """Decode getMeeting return: tuple(address,string,string,uint256,uint256,uint256,uint256,bool).

    Solidity wraps single-struct returns with a tuple offset pointer at word 0.
    Word 0 = 0x20 → actual struct data starts at byte 32.

    Struct layout (8 words head + dynamic tail):
      - 0x00: creator (address, uint256 padded)
      - 0x20: name offset
      - 0x40: description offset
      - 0x60: startTime
      - 0x80: endTime
      - 0xA0: feePerAttendee
      - 0xC0: attendeeCount
      - 0xE0: active (bool as uint256)
    """
    data = hex_str.replace("0x", "")
    if len(data) < 128:
        return None

    def _addr(h: str) -> str:
        return "0x" + h[24:64]

    def _uint(h: str) -> int:
        return int(h, 16)

    def _str_at(hex_data: str, word_offset: int) -> str:
        """Read a string from the hex data at the given word offset."""
        base = word_offset * 64
        if base + 64 > len(hex_data):
            return ""
        str_offset = _uint(hex_data[base:base + 64])
        pos = str_offset * 2
        if pos + 64 > len(hex_data):
            return ""
        length = _uint(hex_data[pos:pos + 64])
        pos += 64
        end = pos + length * 2
        if end > len(hex_data):
            return ""
        return bytes.fromhex(hex_data[pos:end]).decode("utf-8", errors="replace")

    # Solidity returns a tuple-wrapped struct: word 0 is the offset (0x20) to
    # the struct data. Skip it and decode from the struct head proper.
    offset = _uint(data[0:64])
    if offset == 32:
        data = data[offset * 2:]  # skip 64 hex chars

    if len(data) < 512:
        return None

    creator = _addr(data[0:64])
    name_offset = _uint(data[64:128])
    desc_offset = _uint(data[128:192])
    start_time = _uint(data[192:256])
    end_time = _uint(data[256:320])
    fee_per_attendee = _uint(data[320:384])
    attendee_count = _uint(data[384:448])
    active = _uint(data[448:512]) == 1

    name = _str_at(data, name_offset // 32)
    description = _str_at(data, desc_offset // 32)

    return {
        "creator": creator,
        "name": name,
        "description": description,
        "startTime": start_time,
        "endTime": end_time,
        "feePerAttendee": fee_per_attendee,
        "attendeeCount": attendee_count,
        "active": active,
    }


class RpcClient:
    """Async JSON-RPC client for Ethereum read calls."""

    def __init__(self, rpc_url: str, logger: Optional[logging.Logger] = None) -> None:
        self._url = rpc_url
        self._req_id = 0
        self.log = logger or logging.getLogger("eth_rpc")

    def _next_id(self) -> int:
        self._req_id += 1
        return self._req_id

    async def _call(self, to: str, data: str, block: str = "latest") -> str:
        """Execute eth_call and return the hex result."""
        payload = {
            "jsonrpc": "2.0",
            "id": self._next_id(),
            "method": "eth_call",
            "params": [{"to": to, "data": data}, block],
        }
        async with aiohttp.ClientSession() as session:
            async with session.post(self._url, json=payload,
                                    timeout=aiohttp.ClientTimeout(total=10)) as resp:
                body = await resp.json()
        if "error" in body:
            raise RuntimeError(f"RPC error: {body['error']}")
        return body["result"]

    async def balance_of(self, token_addr: str, user_addr: str) -> int:
        """Return ERC20 balanceOf(user) in wei."""
        data = _encode_balance_of(user_addr)
        result = await self._call(token_addr, data)
        return _decode_uint256(result)

    async def get_meeting(self, manager_addr: str, meeting_id: int, block: str = "latest") -> dict[str, Any] | None:
        """Return meeting struct or None. Pass block as hex (e.g. '0x...') to query at a specific block."""
        data = _encode_get_meeting(meeting_id)
        result = await self._call(manager_addr, data, block=block)
        return _decode_get_meeting(result)

    async def has_joined(self, manager_addr: str, meeting_id: int, user_addr: str) -> bool:
        """Return True if user has joined the meeting (paid)."""
        data = _encode_has_joined(meeting_id, user_addr)
        result = await self._call(manager_addr, data)
        return _decode_uint256(result) != 0

    async def creation_fee(self, manager_addr: str) -> int:
        """Return creationFee from MeetingManager."""
        data = _encode_creation_fee()
        result = await self._call(manager_addr, data)
        return _decode_uint256(result)
