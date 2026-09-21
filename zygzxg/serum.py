"""Read Serum 2's XferJson / Zstandard / CBOR container without executing Serum.

The state tree is returned without rewriting unknown keys. Decoding a field is not
equivalent to semantically supporting it in the synthesizer.
"""

from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path
import struct
from typing import Any

import cbor2
import zstandard

MAGIC = b"XferJson\x00"
ZSTD_MAGIC = b"\x28\xb5\x2f\xfd"
MAX_FILE_SIZE = 128 * 1024 * 1024
MAX_STATE_SIZE = 256 * 1024 * 1024


class PresetFormatError(ValueError):
    pass


@dataclass(frozen=True)
class SerumDocument:
    metadata: dict[str, Any]
    state: dict[str, Any]
    container_flags: int


def decode_bytes(data: bytes) -> SerumDocument:
    if len(data) > MAX_FILE_SIZE:
        raise PresetFormatError("preset exceeds configured file-size limit")
    if len(data) < 25 or not data.startswith(MAGIC):
        raise PresetFormatError("missing XferJson header")
    metadata_length, metadata_flags = struct.unpack_from("<II", data, 9)
    metadata_end = 17 + metadata_length
    if metadata_end + 8 > len(data) or metadata_length > MAX_FILE_SIZE:
        raise PresetFormatError("invalid metadata length")
    try:
        metadata = json.loads(data[17:metadata_end])
    except (ValueError, UnicodeDecodeError) as exc:
        raise PresetFormatError("invalid metadata JSON") from exc
    if not isinstance(metadata, dict):
        raise PresetFormatError("metadata must be an object")
    state_length, state_flags = struct.unpack_from("<II", data, metadata_end)
    if state_length > MAX_STATE_SIZE:
        raise PresetFormatError("state exceeds configured decompressed-size limit")
    compressed = data[metadata_end + 8 :]
    if not compressed.startswith(ZSTD_MAGIC):
        raise PresetFormatError("expected a Zstandard frame after metadata")
    try:
        state_bytes = zstandard.ZstdDecompressor().decompress(
            compressed, max_output_size=MAX_STATE_SIZE
        )
    except zstandard.ZstdError as exc:
        raise PresetFormatError("invalid Zstandard payload") from exc
    if len(state_bytes) != state_length:
        raise PresetFormatError("decompressed size differs from container header")
    try:
        state = cbor2.loads(state_bytes)
    except (ValueError, TypeError, EOFError) as exc:
        raise PresetFormatError("invalid CBOR state") from exc
    if not isinstance(state, dict):
        raise PresetFormatError("state must be an object")
    if metadata_flags != 0:
        raise PresetFormatError(f"unknown metadata flags: {metadata_flags}")
    return SerumDocument(metadata=metadata, state=state, container_flags=state_flags)


def read_preset(path: str | Path) -> SerumDocument:
    file_path = Path(path)
    if file_path.stat().st_size > MAX_FILE_SIZE:
        raise PresetFormatError("preset exceeds configured file-size limit")
    return decode_bytes(file_path.read_bytes())


def encode_bytes(document: SerumDocument) -> bytes:
    """Losslessly preserve decoded fields, though CBOR byte order may change."""
    metadata = json.dumps(document.metadata, ensure_ascii=False, separators=(",", ":")).encode()
    state = cbor2.dumps(document.state)
    if len(metadata) > MAX_FILE_SIZE or len(state) > MAX_STATE_SIZE:
        raise PresetFormatError("document exceeds configured size limit")
    compressed = zstandard.ZstdCompressor(level=3).compress(state)
    return (
        MAGIC
        + struct.pack("<II", len(metadata), 0)
        + metadata
        + struct.pack("<II", len(state), document.container_flags)
        + compressed
    )


def populated_objects(state: dict[str, Any]) -> dict[str, Any]:
    """Return nonempty top-level objects; this is not a semantic coverage count."""
    return {
        key: value
        for key, value in state.items()
        if isinstance(value, dict)
        and value
        and value.get("plainParams") != "default"
    }


def count_explicit_parameters(value: Any) -> int:
    if isinstance(value, dict):
        return sum(
            len(child) if key == "plainParams" and isinstance(child, dict)
            else count_explicit_parameters(child)
            for key, child in value.items()
        )
    if isinstance(value, list):
        return sum(count_explicit_parameters(child) for child in value)
    return 0
