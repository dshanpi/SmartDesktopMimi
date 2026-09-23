"""Reference implementation of the AITVBox v2 control-plane framing.

The production C/Go clients must pass the same contract tests. Media payloads
are deliberately excluded; they use the capture service's binary data plane.
"""

from __future__ import annotations

import json
import struct
from dataclasses import dataclass, field
from typing import Any, Iterable


SCHEMA_VERSION = 2
MAX_FRAME_SIZE = 64 * 1024
MESSAGE_TYPES = frozenset(("request", "response", "event"))


class ProtocolError(ValueError):
    """A malformed or unsupported control-plane frame."""

    def __init__(self, code: str, message: str, *, retryable: bool = False):
        super().__init__(message)
        self.code = code
        self.retryable = retryable


def validate_envelope(message: dict[str, Any]) -> dict[str, Any]:
    if not isinstance(message, dict):
        raise ProtocolError("INVALID_ENVELOPE", "message must be a JSON object")
    if message.get("schemaVersion") != SCHEMA_VERSION:
        raise ProtocolError("UNSUPPORTED_VERSION", "schemaVersion must be 2")
    message_type = message.get("messageType")
    if message_type not in MESSAGE_TYPES:
        raise ProtocolError("INVALID_MESSAGE_TYPE", "unsupported messageType")
    topic = message.get("topic")
    if not isinstance(topic, str) or not topic or len(topic) > 128:
        raise ProtocolError("INVALID_TOPIC", "topic must be a non-empty string")
    if not isinstance(message.get("payload"), dict):
        raise ProtocolError("INVALID_PAYLOAD", "payload must be an object")
    if message_type in ("request", "response"):
        request_id = message.get("requestId")
        if not isinstance(request_id, str) or not request_id or len(request_id) > 128:
            raise ProtocolError("INVALID_REQUEST_ID", "requestId is required")
    if message_type == "event":
        sequence = message.get("sequence")
        if not isinstance(sequence, int) or isinstance(sequence, bool) or sequence < 0:
            raise ProtocolError("INVALID_SEQUENCE", "event sequence must be non-negative")
    error = message.get("error")
    if error is not None:
        if not isinstance(error, dict):
            raise ProtocolError("INVALID_ERROR", "error must be an object")
        if not isinstance(error.get("code"), str) or not error["code"]:
            raise ProtocolError("INVALID_ERROR", "error.code is required")
        if not isinstance(error.get("message"), str):
            raise ProtocolError("INVALID_ERROR", "error.message is required")
        if not isinstance(error.get("retryable"), bool):
            raise ProtocolError("INVALID_ERROR", "error.retryable is required")
    return message


def encode(message: dict[str, Any]) -> bytes:
    validate_envelope(message)
    payload = json.dumps(
        message, ensure_ascii=False, separators=(",", ":"), sort_keys=True
    ).encode("utf-8")
    if not payload or len(payload) > MAX_FRAME_SIZE:
        raise ProtocolError("FRAME_TOO_LARGE", "control frame exceeds 64 KiB")
    return struct.pack(">I", len(payload)) + payload


@dataclass
class Decoder:
    """Incremental decoder supporting split and coalesced stream frames."""

    _buffer: bytearray = field(default_factory=bytearray)

    def feed(self, data: bytes) -> list[dict[str, Any]]:
        if not isinstance(data, (bytes, bytearray, memoryview)):
            raise TypeError("data must be bytes-like")
        self._buffer.extend(data)
        messages: list[dict[str, Any]] = []
        while len(self._buffer) >= 4:
            length = struct.unpack(">I", self._buffer[:4])[0]
            if length == 0:
                self._buffer.clear()
                raise ProtocolError("EMPTY_FRAME", "zero-length frames are forbidden")
            if length > MAX_FRAME_SIZE:
                self._buffer.clear()
                raise ProtocolError("FRAME_TOO_LARGE", "control frame exceeds 64 KiB")
            if len(self._buffer) < 4 + length:
                break
            raw = bytes(self._buffer[4 : 4 + length])
            del self._buffer[: 4 + length]
            try:
                decoded = json.loads(raw.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                raise ProtocolError("INVALID_JSON", "frame is not valid UTF-8 JSON") from exc
            messages.append(validate_envelope(decoded))
        return messages


def decode_chunks(chunks: Iterable[bytes]) -> list[dict[str, Any]]:
    decoder = Decoder()
    result: list[dict[str, Any]] = []
    for chunk in chunks:
        result.extend(decoder.feed(chunk))
    return result
