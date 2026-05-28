"""Room manager for Pilot Center.

RoomManager maintains a mapping of room_id -> Room and provides convenience
methods to create/delete rooms and to manage users inside rooms.

This implementation is synchronous and lightweight; it is intended to be
called from asyncio tasks in the pilot_center application. Methods raise
ValueError for illegal operations (e.g. creating an already-existing room)
or return booleans for idempotent operations.
"""

from __future__ import annotations

from typing import Dict, List, Optional, TYPE_CHECKING
from threading import RLock
import weakref
import asyncio

from .room import Room
from .user import User

if TYPE_CHECKING:
	# Avoid circular import at runtime; sessions must implement async send_* methods
	from typing import Protocol

	class _SessionLike(Protocol):
		async def send_response_ok(self, req_id: object, data: Optional[Dict[str, object]] = None) -> None: ...
		async def send_response_error(self, req_id: object, code: int = 0, reason: str = "") -> None: ...
		async def send_notification(self, method: str, data: Optional[Dict[str, object]] = None) -> None: ...


class RoomManager:
	"""Manage multiple Room instances.

	Thread-safety: internal lock (RLock) protects modifications. For the
	simple pilot_center use case this is sufficient.
	"""

	def __init__(self) -> None:
		self._rooms: Dict[str, Room] = {}
		self._room_tokens: Dict[str, str] = {}  # room_id -> room_token
		self._lock = RLock()
		# NOTE: session associations are stored on User and Room objects.
		# RoomManager no longer keeps a separate user->session map.

	def handle_join(self, room_id: str, user_id: str, user_name: str = "", audience: bool = False, session: object | None = None) -> bool:
		"""Handle a user joining a room. Create room if missing.

		Returns True if the user was added, False if already present.
		"""
		with self._lock:
			room = self.get_or_create_room(room_id)
			return room.handle_join(user_id=user_id, user_name=user_name or "", audience=audience, session=session)

	def register_room_token(self, room_id: str, room_token: str) -> None:
		"""Register a room token for later validation."""
		with self._lock:
			self._room_tokens[room_id] = room_token

	def validate_room_token(self, room_id: str, room_token: str) -> bool:
		"""Validate that room_token matches the registered token for room_id.
		Returns False if no token was registered for this room (roomId not created via API)."""
		with self._lock:
			expected = self._room_tokens.get(room_id)
			if expected is None:
				return False  # roomId not created through API, reject
			if expected == room_token:
				return True  # token matches, allow
			self.log.warning("Invalid room token for roomId %s: expected %s, got %s", room_id, expected, room_token)
			return False  # token does not match, reject

	def handle_push_notification(self, data: dict, session: object) -> bool:
		"""Handle a push notification from a user session."""
		with self._lock:
			# get roomId from data
			room_id = data.get("roomId")
			if not room_id:
				return False
			room = self.get_or_create_room(room_id)
			# Forward the notification to the room
			room.handle_push_notification(data, session)
			return True
	def handle_pull_remote_stream_notification(self, data: dict, session: object) -> bool:
		"""Handle a pull remote stream notification from a user session."""
		with self._lock:
			# get roomId from data
			room_id = data.get("roomId")
			if not room_id:
				return False
			room = self.get_or_create_room(room_id)
			# Forward the notification to the room
			room.handle_pull_remote_stream_notification(data, session)
			return True
	def handle_userDisconnect_notification(self, data: dict, session: object) -> bool:
		"""Handle a user disconnect notification from a user session."""
		with self._lock:
			# get roomId from data
			room_id = data.get("roomId")
			if not room_id:
				return False
			room = self.get_or_create_room(room_id)
			# Forward the notification to the room
			room.handle_userDisconnect_notification(data, session)
			return True
	def handle_userLeave_notification(self, data: dict, session: object) -> bool:
		"""Handle a user leave notification from a user session."""
		with self._lock:
			# get roomId from data
			room_id = data.get("roomId")
			if not room_id:
				return False
			room = self.get_or_create_room(room_id)
			# Forward the notification to the room
			room.handle_userLeave_notification(data, session)
			return True
	def handle_textMessage_notification(self, data: dict, session: object) -> bool:
		"""Handle a text message notification from a user session."""
		with self._lock:
			# get roomId from data
			room_id = data.get("roomId")
			if not room_id:
				return False
			room = self.get_or_create_room(room_id)
			# Forward the notification to the room
			room.handle_textMessage_notification(data, session)
			return True
	def handle_asr_result_notification(self, data: dict, session: object) -> bool:
		"""Handle an ASR result notification from a user session."""
		with self._lock:
			# get roomId from data
			room_id = data.get("roomId")
			if not room_id:
				return False
			room = self.get_or_create_room(room_id)
			# Forward the notification to the room
			room.handle_asr_result_notification(data, session)
			return True
	
	def get_or_create_room(self, room_id: str) -> Room:
		"""Return the Room for `room_id`, creating it if missing.

		This is thread-safe: the internal lock protects the lookup and
		creation so callers can rely on a single Room instance per id.
		"""
		with self._lock:
			room = self._rooms.get(room_id)
			if room is None:
				room = Room(room_id)
				self._rooms[room_id] = room
			return room

	def delete_room(self, room_id: str) -> bool:
		"""Delete a room. Returns True if deleted, False if not found."""
		with self._lock:
			return self._rooms.pop(room_id, None) is not None

	def list_rooms(self) -> List[Room]:
		with self._lock:
			return list(self._rooms.values())


	# ---- session registration / notification helpers ----


	def notify_user(self, room_id: str, user_id: str, method: str, data: Optional[Dict[str, object]] = None) -> bool:
		"""Fire-and-forget: send a notification to a user if their session is registered.

		Returns True if a task was scheduled, False otherwise.
		"""
		user = self.get_user(room_id, user_id)
		if user is None:
			return False
		# schedule send to all user sessions
		try:
			loop = asyncio.get_running_loop()
		except RuntimeError:
			return False
		try:
			s = user.get_session()
		except Exception:
			s = None
		if s is None:
			return False
		try:
			loop.create_task(s.send_notification(method, data))
		except Exception:
			pass
		return True

	def call_send_response_ok(self, room_id: str, user_id: str, req_id: object, data: Optional[Dict[str, object]] = None) -> bool:
		"""Schedule send_response_ok on the user's sessions. Returns True if scheduled."""
		user = self.get_user(room_id, user_id)
		if user is None:
			return False
		try:
			loop = asyncio.get_running_loop()
		except RuntimeError:
			return False
		try:
			s = user.get_session()
		except Exception:
			s = None
		if s is None:
			return False
		try:
			loop.create_task(s.send_response_ok(req_id, data))
		except Exception:
			pass
		return True

	def call_send_response_error(self, room_id: str, user_id: str, req_id: object, code: int, reason: str) -> bool:
		user = self.get_user(room_id, user_id)
		if user is None:
			return False
		try:
			loop = asyncio.get_running_loop()
		except RuntimeError:
			return False
		try:
			s = user.get_session()
		except Exception:
			s = None
		if s is None:
			return False
		try:
			loop.create_task(s.send_response_error(req_id, code, reason))
		except Exception:
			pass
		return True

	def add_user_to_room(self, room_id: str, user: User) -> bool:
		"""Add user to specified room. Return True if added, False if already present or room missing."""
		with self._lock:
			room = self._rooms.get(room_id)
			if not room:
				return False
			return room.add_user(user)

	def remove_user_from_room(self, room_id: str, user_id: str) -> bool:
		with self._lock:
			room = self._rooms.get(room_id)
			if not room:
				return False
			return room.remove_user(user_id)

	def get_user(self, room_id: str, user_id: str) -> Optional[User]:
		with self._lock:
			room = self._rooms.get(room_id)
			if not room:
				return None
			return room.get_user(user_id)

	def list_users(self, room_id: str) -> List[User]:
		with self._lock:
			room = self._rooms.get(room_id)
			if not room:
				return []
			return room.list_users()

	def room_user_count(self, room_id: str) -> int:
		with self._lock:
			room = self._rooms.get(room_id)
			if not room:
				return 0
			return room.user_count()

