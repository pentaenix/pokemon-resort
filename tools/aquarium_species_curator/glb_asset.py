"""Read animation metadata and prepare GLBZ assets for the RAE viewport."""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import struct

GLBZ_HEADER = struct.Struct("<8sQQ32s")
GLBZ_MAGIC = b"PRGLBZ01"
GLB_HEADER = struct.Struct("<4sII")
GLB_CHUNK_HEADER = struct.Struct("<II")
JSON_CHUNK = 0x4E4F534A


def decode_glbz(container: bytes) -> bytes:
    if len(container) < GLBZ_HEADER.size:
        raise ValueError("GLBZ container is smaller than its header")
    magic, original_size, compressed_size, expected_hash = GLBZ_HEADER.unpack_from(container)
    if magic != GLBZ_MAGIC:
        raise ValueError("Unsupported GLBZ magic/version")
    payload = container[GLBZ_HEADER.size:]
    if len(payload) != compressed_size:
        raise ValueError("GLBZ payload size does not match its header")
    try:
        from compression import zstd
        restored = zstd.decompress(payload)
    except ImportError:
        import zstandard
        restored = zstandard.ZstdDecompressor().decompress(payload, max_output_size=original_size)
    if len(restored) != original_size:
        raise ValueError("GLBZ restored size does not match its header")
    if hashlib.sha256(restored).digest() != expected_hash:
        raise ValueError("GLBZ SHA-256 verification failed")
    return restored


def prepare_preview_asset(source: Path, cache_root: Path) -> Path:
    if source.suffix.casefold() == ".glb":
        return source
    container = source.read_bytes()
    digest = hashlib.sha256(container).hexdigest()[:20]
    destination = cache_root / f"{source.stem}-{digest}.glb"
    if destination.exists():
        return destination
    cache_root.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(".glb.tmp")
    temporary.write_bytes(decode_glbz(container))
    temporary.replace(destination)
    return destination


def glb_document(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) < GLB_HEADER.size:
        raise ValueError("GLB header is truncated")
    magic, version, total_size = GLB_HEADER.unpack_from(data)
    if magic != b"glTF" or version != 2 or total_size != len(data):
        raise ValueError("Expected a complete GLB 2.0 asset")
    cursor = GLB_HEADER.size
    while cursor + GLB_CHUNK_HEADER.size <= len(data):
        size, kind = GLB_CHUNK_HEADER.unpack_from(data, cursor)
        cursor += GLB_CHUNK_HEADER.size
        chunk = data[cursor:cursor + size]
        cursor += size
        if kind == JSON_CHUNK:
            return json.loads(chunk.decode("utf-8").rstrip("\x00 \t\r\n"))
    raise ValueError("GLB has no JSON chunk")


def animation_names(path: Path) -> list[str]:
    names = []
    for index, animation in enumerate(glb_document(path).get("animations", [])):
        name = str(animation.get("name") or f"animation_{index}")
        if name not in names:
            names.append(name)
    return names


def transformed_preview_asset(
    source: Path,
    cache_root: Path,
    pitch_degrees: float,
    yaw_degrees: float,
    scale_multiplier: float,
) -> Path:
    """Wrap each GLB scene in a preview-only transform node."""
    if abs(pitch_degrees) < 0.001 and abs(yaw_degrees) < 0.001 and abs(scale_multiplier - 1.0) < 0.001:
        return source
    signature = f"{source.resolve()}:{source.stat().st_mtime_ns}:{pitch_degrees:.3f}:{yaw_degrees:.3f}:{scale_multiplier:.3f}"
    destination = cache_root / f"{source.stem}-pose-{hashlib.sha256(signature.encode()).hexdigest()[:16]}.glb"
    if destination.exists():
        return destination

    data = source.read_bytes()
    document = glb_document(source)
    nodes = document.setdefault("nodes", [])
    pitch = math.radians(pitch_degrees) * 0.5
    yaw = math.radians(yaw_degrees) * 0.5
    rotation = [
        math.sin(pitch) * math.cos(yaw),
        math.cos(pitch) * math.sin(yaw),
        -math.sin(pitch) * math.sin(yaw),
        math.cos(pitch) * math.cos(yaw),
    ]
    for scene in document.get("scenes", []):
        roots = list(scene.get("nodes", []))
        wrapper = len(nodes)
        nodes.append({
            "name": "AquariumCuratorPreviewTransform",
            "children": roots,
            "rotation": rotation,
            "scale": [scale_multiplier] * 3,
        })
        scene["nodes"] = [wrapper]

    json_bytes = json.dumps(document, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    json_bytes += b" " * ((4 - len(json_bytes) % 4) % 4)
    chunks = [struct.pack("<II", len(json_bytes), JSON_CHUNK) + json_bytes]
    cursor = GLB_HEADER.size
    first_chunk = True
    while cursor + GLB_CHUNK_HEADER.size <= len(data):
        size, kind = GLB_CHUNK_HEADER.unpack_from(data, cursor)
        chunk_end = cursor + GLB_CHUNK_HEADER.size + size
        if not first_chunk:
            chunks.append(data[cursor:chunk_end])
        first_chunk = False
        cursor = chunk_end
    body = b"".join(chunks)
    output = GLB_HEADER.pack(b"glTF", 2, GLB_HEADER.size + len(body)) + body
    cache_root.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(".glb.tmp")
    temporary.write_bytes(output)
    temporary.replace(destination)
    return destination
