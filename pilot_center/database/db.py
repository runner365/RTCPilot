#!/usr/bin/env python3
"""PostgreSQL database layer for Pilot Center."""
from __future__ import annotations

import logging
import re
import secrets
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Optional

import asyncpg


# Ethereum address: 0x followed by 40 hex chars
_ETH_ADDR_RE = re.compile(r"^0x[a-fA-F0-9]{40}$")


def is_valid_eth_address(address: str) -> bool:
    return bool(_ETH_ADDR_RE.match(address))


def normalize_address(address: str) -> str:
    return address.lower().strip()


def generate_nonce() -> str:
    return secrets.token_hex(32)


def generate_token() -> str:
    return secrets.token_hex(32)


@dataclass
class UserRecord:
    address: str
    nonce: str = ""
    created_at: datetime = field(default_factory=lambda: datetime.now(timezone.utc))
    last_login_at: datetime = field(default_factory=lambda: datetime.now(timezone.utc))


class Database:
    """Async PostgreSQL connection pool and user operations."""

    def __init__(self, host: str, port: int, user: str, password: str, database: str,
                 logger: Optional[logging.Logger] = None) -> None:
        self._dsn = f"postgresql://{user}:{password}@{host}:{port}/{database}"
        self._pool: Optional[asyncpg.Pool] = None
        self.log = logger or logging.getLogger("database")

    async def start(self) -> None:
        self._pool = await asyncpg.create_pool(self._dsn, min_size=2, max_size=10)
        await self._ensure_tables()
        self.log.info("Database pool created and tables ready")

    async def stop(self) -> None:
        if self._pool is not None:
            await self._pool.close()
            self._pool = None
        self.log.info("Database pool closed")

    async def _ensure_tables(self) -> None:
        assert self._pool is not None
        async with self._pool.acquire() as conn:
            await conn.execute("""
                CREATE TABLE IF NOT EXISTS users (
                    id             SERIAL PRIMARY KEY,
                    address        VARCHAR(42) UNIQUE NOT NULL,
                    nonce          VARCHAR(64) NOT NULL DEFAULT '',
                    created_at     TIMESTAMPTZ NOT NULL DEFAULT NOW(),
                    last_login_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
                );
            """)
            await conn.execute("""
                CREATE INDEX IF NOT EXISTS idx_users_address ON users (address);
            """)

            await conn.execute("""
                CREATE TABLE IF NOT EXISTS sessions (
                    id          SERIAL PRIMARY KEY,
                    address     VARCHAR(42) NOT NULL REFERENCES users(address),
                    token       VARCHAR(64) UNIQUE NOT NULL,
                    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
                );
            """)
            await conn.execute("""
                CREATE INDEX IF NOT EXISTS idx_sessions_token ON sessions (token);
            """)

            await conn.execute("""
                CREATE TABLE IF NOT EXISTS room_meetings (
                    id          SERIAL PRIMARY KEY,
                    meeting_id  BIGINT UNIQUE NOT NULL,
                    room_id     VARCHAR(32) UNIQUE NOT NULL,
                    creator     VARCHAR(42) NOT NULL,
                    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
                );
            """)
            await conn.execute("""
                CREATE INDEX IF NOT EXISTS idx_room_meetings_meeting
                    ON room_meetings (meeting_id);
            """)

            # Add token_balance columns if they don't exist (idempotent migration)
            await conn.execute("""
                DO $$
                BEGIN
                    IF NOT EXISTS (
                        SELECT 1 FROM information_schema.columns
                        WHERE table_name='users' AND column_name='token_balance'
                    ) THEN
                        ALTER TABLE users ADD COLUMN token_balance NUMERIC DEFAULT 0;
                    END IF;
                    IF NOT EXISTS (
                        SELECT 1 FROM information_schema.columns
                        WHERE table_name='users' AND column_name='token_balance_updated_at'
                    ) THEN
                        ALTER TABLE users ADD COLUMN token_balance_updated_at TIMESTAMPTZ;
                    END IF;
                END $$;
            """)

    # ------------------------------------------------------------------
    # User operations
    # ------------------------------------------------------------------

    async def get_or_create_user(self, address: str) -> UserRecord:
        """Return existing user or create a new one. Never raises on valid input."""
        if not is_valid_eth_address(address):
            raise ValueError(f"Invalid Ethereum address: {address}")

        lower = normalize_address(address)
        now = datetime.now(timezone.utc)

        assert self._pool is not None
        async with self._pool.acquire() as conn:
            row = await conn.fetchrow(
                """
                INSERT INTO users (address, created_at, last_login_at)
                VALUES ($1, $2, $2)
                ON CONFLICT (address) DO NOTHING
                RETURNING address, nonce, created_at, last_login_at
                """,
                lower, now,
            )
            if row is None:
                row = await conn.fetchrow(
                    "SELECT address, nonce, created_at, last_login_at FROM users WHERE address = $1",
                    lower,
                )

        return UserRecord(
            address=row["address"],
            nonce=row["nonce"],
            created_at=row["created_at"],
            last_login_at=row["last_login_at"],
        )

    async def update_user_nonce(self, address: str, nonce: str) -> None:
        """Store a new challenge nonce for the user."""
        lower = normalize_address(address)

        assert self._pool is not None
        async with self._pool.acquire() as conn:
            await conn.execute(
                "UPDATE users SET nonce = $2 WHERE address = $1",
                lower, nonce,
            )

    async def get_user(self, address: str) -> Optional[UserRecord]:
        """Look up a user by address."""
        if not is_valid_eth_address(address):
            return None

        lower = normalize_address(address)

        assert self._pool is not None
        async with self._pool.acquire() as conn:
            row = await conn.fetchrow(
                "SELECT address, nonce, created_at, last_login_at FROM users WHERE address = $1",
                lower,
            )

        if row is None:
            return None
        return UserRecord(
            address=row["address"],
            nonce=row["nonce"],
            created_at=row["created_at"],
            last_login_at=row["last_login_at"],
        )

    async def login_user(self, address: str) -> UserRecord:
        """Update last_login_at and return the user record."""
        lower = normalize_address(address)
        now = datetime.now(timezone.utc)

        assert self._pool is not None
        async with self._pool.acquire() as conn:
            row = await conn.fetchrow(
                """
                UPDATE users SET last_login_at = $2 WHERE address = $1
                RETURNING address, nonce, created_at, last_login_at
                """,
                lower, now,
            )

        if row is None:
            raise ValueError(f"User not found: {address}")
        return UserRecord(
            address=row["address"],
            nonce=row["nonce"],
            created_at=row["created_at"],
            last_login_at=row["last_login_at"],
        )

    # ------------------------------------------------------------------
    # Session operations
    # ------------------------------------------------------------------

    async def create_session(self, address: str) -> str:
        """Create a new session for address, return the token."""
        lower = normalize_address(address)
        token = secrets.token_hex(32)

        assert self._pool is not None
        async with self._pool.acquire() as conn:
            await conn.execute(
                "INSERT INTO sessions (address, token) VALUES ($1, $2)",
                lower, token,
            )
        return token

    async def verify_token(self, token: str) -> str | None:
        """Return the address for a valid token, or None."""
        assert self._pool is not None
        async with self._pool.acquire() as conn:
            row = await conn.fetchrow(
                "SELECT address FROM sessions WHERE token = $1",
                token,
            )
        if row is None:
            return None
        return row["address"]

    # ------------------------------------------------------------------
    # Token balance
    # ------------------------------------------------------------------

    async def update_token_balance(self, address: str, balance: int) -> None:
        """Cache token balance for a user."""
        lower = normalize_address(address)
        now = datetime.now(timezone.utc)
        assert self._pool is not None
        async with self._pool.acquire() as conn:
            await conn.execute(
                "UPDATE users SET token_balance = $2, token_balance_updated_at = $3 WHERE address = $1",
                lower, str(balance), now,
            )

    async def get_token_balance(self, address: str) -> tuple[int, datetime | None]:
        """Return (balance, updated_at) for a user. Balance is 0 if not cached."""
        lower = normalize_address(address)
        assert self._pool is not None
        async with self._pool.acquire() as conn:
            row = await conn.fetchrow(
                "SELECT token_balance, token_balance_updated_at FROM users WHERE address = $1",
                lower,
            )
        if row is None or row["token_balance"] is None:
            return 0, None
        try:
            bal = int(row["token_balance"])
        except (ValueError, TypeError):
            bal = 0
        return bal, row["token_balance_updated_at"]

    # ------------------------------------------------------------------
    # Room-meeting mapping
    # ------------------------------------------------------------------

    async def create_room_meeting(self, meeting_id: int, room_id: str,
                                   creator: str) -> bool:
        """Link a meetingId to a roomId. Returns False if meeting_id already used."""
        lower = normalize_address(creator)

        assert self._pool is not None
        async with self._pool.acquire() as conn:
            try:
                await conn.execute(
                    """
                    INSERT INTO room_meetings (meeting_id, room_id, creator)
                    VALUES ($1, $2, $3)
                    """,
                    meeting_id, room_id, lower,
                )
                return True
            except asyncpg.exceptions.UniqueViolationError:
                return False

    async def get_room_by_meeting(self, meeting_id: int) -> str | None:
        """Return room_id if this meetingId was already used, else None."""
        assert self._pool is not None
        async with self._pool.acquire() as conn:
            row = await conn.fetchrow(
                "SELECT room_id FROM room_meetings WHERE meeting_id = $1",
                meeting_id,
            )
        if row is None:
            return None
        return row["room_id"]
