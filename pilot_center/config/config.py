#!/usr/bin/env python3
"""
Simple Config loader for WebSocket Protoo center.

Reads YAML and exposes:
  - listen_ip
  - listen_port
  - cert_path
  - key_path

CLI:
  python pilot_center/websocket_protoo/config/config.py pilot_center/websocket_protoo/center.yaml
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any, Dict


class Config:
    """Load center YAML and expose websocket fields."""

    listen_ip: str
    listen_port: int
    http_port: int
    cert_path: str
    key_path: str

    # database
    db_host: str
    db_port: int
    db_user: str
    db_password: str
    db_database: str

    # ethereum
    eth_rpc_url: str

    # meeting contract addresses
    meeting_token: str
    uniswap_liquidity_setup: str
    meeting_manager: str

    def __init__(self, yaml_path: str | Path) -> None:
        p = Path(yaml_path)
        data = self._load_yaml(p)

        # --- websocket section ---
        ws = data.get("websocket")
        if not isinstance(ws, dict):
            raise SystemExit("Missing or invalid 'websocket' section in YAML")

        for k in ("listen_ip", "listen_port", "cert_path", "key_path"):
            if k not in ws:
                raise SystemExit(f"Missing key in websocket section: {k}")

        self.listen_ip = str(ws.get("listen_ip", ""))
        try:
            self.listen_port = int(ws.get("listen_port", 0))
        except Exception as e:
            raise SystemExit(f"Invalid listen_port: {ws.get('listen_port')}") from e
        try:
            self.http_port = int(ws.get("http_port", 9080))
        except Exception as e:
            raise SystemExit(f"Invalid http_port: {ws.get('http_port')}") from e

        self.cert_path = self._resolve_path(p, ws.get("cert_path"))
        self.key_path = self._resolve_path(p, ws.get("key_path"))

        # --- database section ---
        db = data.get("database")
        if not isinstance(db, dict):
            raise SystemExit("Missing or invalid 'database' section in YAML")

        for k in ("host", "port", "user", "password", "database"):
            if k not in db:
                raise SystemExit(f"Missing key in database section: {k}")

        self.db_host = str(db.get("host", "127.0.0.1"))
        try:
            self.db_port = int(db.get("port", 5432))
        except Exception as e:
            raise SystemExit(f"Invalid db port: {db.get('port')}") from e
        self.db_user = str(db.get("user", ""))
        self.db_password = str(db.get("password", ""))
        self.db_database = str(db.get("database", ""))

        # --- ethereum section ---
        eth = data.get("ethereum")
        if not isinstance(eth, dict):
            raise SystemExit("Missing or invalid 'ethereum' section in YAML")

        if "rpc_url" not in eth:
            raise SystemExit("Missing key in ethereum section: rpc_url")
        self.eth_rpc_url = str(eth.get("rpc_url", ""))

        # --- meetingcontract section ---
        mc = data.get("meetingcontract")
        if not isinstance(mc, dict):
            raise SystemExit("Missing or invalid 'meetingcontract' section in YAML")

        for k in ("meeting_token", "uniswap_liquidity_setup", "meeting_manager"):
            if k not in mc:
                raise SystemExit(f"Missing key in meetingcontract section: {k}")

        self.meeting_token = str(mc.get("meeting_token", ""))
        self.uniswap_liquidity_setup = str(mc.get("uniswap_liquidity_setup", ""))
        self.meeting_manager = str(mc.get("meeting_manager", ""))

    @staticmethod
    def _ensure_yaml() -> None:
        try:
            import yaml  # noqa: F401
        except ModuleNotFoundError:
            print(
                "PyYAML is not installed. Install it with:\n  python -m pip install PyYAML",
                file=sys.stderr,
            )
            raise SystemExit(2)

    @classmethod
    def _load_yaml(cls, path: Path) -> Dict[str, Any]:
        if not path.exists():
            print(f"Config file does not exist: {path}", file=sys.stderr)
            raise SystemExit(2)
        if not path.is_file():
            print(f"Path is not a file: {path}", file=sys.stderr)
            raise SystemExit(2)

        cls._ensure_yaml()
        import yaml  # type: ignore
        try:
            with path.open("r", encoding="utf-8") as f:
                data = yaml.safe_load(f) or {}
        except yaml.YAMLError as e:  # type: ignore[attr-defined]
            print(f"Failed to parse YAML: {e}", file=sys.stderr)
            raise SystemExit(2)
        except OSError as e:
            print(f"Failed to read file: {e}", file=sys.stderr)
            raise SystemExit(2)
        if not isinstance(data, dict):
            print("YAML root should be a mapping (dict)", file=sys.stderr)
            raise SystemExit(2)
        return data

    @staticmethod
    def _resolve_path(yaml_file: Path, path_value: Any) -> str:
        s = "" if path_value is None else str(path_value)
        if not s:
            return s
        p = Path(s)
        if p.is_absolute():
            return str(p)
        return str((yaml_file.parent / p).resolve())

    def dump(self) -> str:
        """Dump selected fields as YAML-ish string."""
        try:
            import yaml  # type: ignore
            data = {
                "websocket": {
                    "listen_ip": self.listen_ip,
                    "listen_port": self.listen_port,
                    "cert_path": self.cert_path,
                    "key_path": self.key_path,
                }
            }
            return yaml.safe_dump(data, sort_keys=False, allow_unicode=True)
        except Exception:
            return (
                "websocket:\n"
                f"  listen_ip: {self.listen_ip}\n"
                f"  listen_port: {self.listen_port}\n"
                f"  cert_path: {self.cert_path}\n"
                f"  key_path: {self.key_path}\n"
            )


def _parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Load center.yaml and print selected fields",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("config", type=Path, help="Path to YAML configuration file")
    return parser.parse_args(argv)