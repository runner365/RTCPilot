"""SFU Manager.

Manages `Sfu` instances keyed by `ws_url`.
"""
from __future__ import annotations

import logging
from typing import Dict, Optional, Iterable

from .sfu import Sfu


class SfuManager:
    """Manage `Sfu` objects in a dictionary keyed by `ws_url`."""

    def __init__(self, logger: Optional[logging.Logger] = None) -> None:
        self._log = logger or logging.getLogger("sfu.manager")
        self._items: Dict[str, Sfu] = {}

    def add_or_update(self, session: object, ws_url: str,
                      alive_ms: Optional[int] = None) -> Sfu:
        """Add a new `Sfu` or update existing one with latest session."""
        if not isinstance(ws_url, str) or not ws_url:
            raise ValueError("ws_url must be a non-empty string")
        item = self._items.get(ws_url)
        if item is None:
            item = Sfu(ws_url=ws_url, session=session)
            self._items[ws_url] = item
            self._log.info("SFU created: %s", ws_url)
        else:
            item.session = session
        if alive_ms is not None:
            item.alive_ms = int(alive_ms)
        else:
            item.touch()
        return item

    def get(self, ws_url: str) -> Optional[Sfu]:
        return self._items.get(ws_url)

    def remove(self, ws_url: str) -> bool:
        if ws_url in self._items:
            del self._items[ws_url]
            self._log.info("SFU removed: %s", ws_url)
            return True
        return False

    def touch(self, ws_url: str) -> None:
        item = self._items.get(ws_url)
        if item is not None:
            item.touch()

    def remove_by_session(self, session: object) -> Optional[str]:
        """Remove the SFU whose session matches, return its ws_url or None."""
        for ws_url, item in list(self._items.items()):
            if item.session is session:
                del self._items[ws_url]
                self._log.info("SFU removed by session: %s", ws_url)
                return ws_url
        return None

    def list_ws_urls(self) -> Iterable[str]:
        return self._items.keys()

    def prune_stale(self, ttl_ms: int, now_ms: Optional[int] = None) -> list[str]:
        """Remove SFUs that are not alive within `ttl_ms`. Return removed ws_urls."""
        removed: list[str] = []
        for ws_url, item in list(self._items.items()):
            if not item.is_alive(ttl_ms, now_ms):
                removed.append(ws_url)
                del self._items[ws_url]
        if removed:
            self._log.info("Pruned stale SFUs: %s", ", ".join(removed))
        return removed
