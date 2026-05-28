"""SFU (Selective Forwarding Unit) module.

Defines `Sfu`: represents a WebRTC SFU service connected to Pilot Center.
"""
from __future__ import annotations

import time
import logging
from typing import Optional


def _now_ms() -> int:
    return int(time.time() * 1000)


class Sfu:
    """Represents a connected WebRTC SFU service."""

    def __init__(self, ws_url: str, session: object,
                 log: Optional[logging.Logger] = None) -> None:
        self.ws_url = ws_url
        self.session = session
        self.alive_ms = _now_ms()
        self.log = log or logging.getLogger("sfu")

    def touch(self, now_ms: Optional[int] = None) -> None:
        self.alive_ms = _now_ms() if now_ms is None else int(now_ms)

    def ms_since_alive(self, now_ms: Optional[int] = None) -> int:
        now = _now_ms() if now_ms is None else int(now_ms)
        return max(0, now - self.alive_ms)

    def is_alive(self, ttl_ms: int, now_ms: Optional[int] = None) -> bool:
        return self.ms_since_alive(now_ms) <= int(ttl_ms)

    def __repr__(self) -> str:
        return (f"Sfu(ws_url={self.ws_url!r}, alive_ms={self.alive_ms}, "
                f"peer={getattr(self.session, 'peer', None)!r})")
