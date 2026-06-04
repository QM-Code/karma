#!/usr/bin/env python3
"""Bake the authored EXR explosion sequences into runtime PNG atlases.

The engine prefab texture path uploads RGBA8 textures, so the direct runtime
asset is a PNG atlas while the EXR files remain the source material.
"""

from __future__ import annotations

from pathlib import Path
import struct

import numpy as np
from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
TEXTURE_DIR = ROOT / "textures"
FRAME_SIZE = 400
COLUMNS = 5
ROWS = 5
BORDER = 4
SPACING = 4
ATLAS_SIZE = COLUMNS * FRAME_SIZE + (COLUMNS - 1) * SPACING + BORDER * 2
FRAME_NUMBERS = [round(1 + i * 99 / 24) for i in range(25)]


def read_cstr(data: bytes, position: int) -> tuple[str, int]:
  end = data.index(0, position)
  return data[position:end].decode("latin1"), end + 1


def read_exr_rgba(path: Path) -> np.ndarray:
  data = path.read_bytes()
  magic, _version = struct.unpack_from("<II", data, 0)
  if magic != 20000630:
    raise ValueError(f"{path}: not an OpenEXR file")

  position = 8
  channels: list[tuple[str, int, int, int]] = []
  attrs: dict[str, tuple[str, bytes]] = {}
  while True:
    name, position = read_cstr(data, position)
    if not name:
      break
    attr_type, position = read_cstr(data, position)
    size = struct.unpack_from("<I", data, position)[0]
    position += 4
    value = data[position:position + size]
    position += size
    attrs[name] = (attr_type, value)
    if name == "channels":
      channel_position = 0
      while channel_position < len(value):
        channel_name, channel_position = read_cstr(value, channel_position)
        if not channel_name:
          break
        pixel_type = struct.unpack_from("<i", value, channel_position)[0]
        channel_position += 4
        channel_position += 4  # pLinear plus reserved bytes.
        x_sampling, y_sampling = struct.unpack_from("<ii", value, channel_position)
        channel_position += 8
        channels.append((channel_name, pixel_type, x_sampling, y_sampling))

  compression = attrs.get("compression", (None, b"\xff"))[1][0]
  if compression != 0:
    raise ValueError(f"{path}: only uncompressed scanline EXR is supported")

  xmin, ymin, xmax, ymax = struct.unpack_from("<iiii", attrs["dataWindow"][1], 0)
  width = xmax - xmin + 1
  height = ymax - ymin + 1
  if width != FRAME_SIZE or height != FRAME_SIZE:
    raise ValueError(f"{path}: expected {FRAME_SIZE}x{FRAME_SIZE}, got {width}x{height}")

  planes = {name: np.zeros((height, width), dtype=np.float32) for name, _, _, _ in channels}
  offset_table_position = position
  for row_index in range(height):
    block_offset = struct.unpack_from("<Q", data, offset_table_position + row_index * 8)[0]
    y, data_size = struct.unpack_from("<iI", data, block_offset)
    scanline_position = block_offset + 8
    out_row = y - ymin
    for name, pixel_type, x_sampling, y_sampling in channels:
      if pixel_type != 1 or x_sampling != 1 or y_sampling != 1:
        raise ValueError(f"{path}: unsupported channel {name}")
      byte_count = width * 2
      planes[name][out_row, :] = np.frombuffer(
          data,
          dtype="<f2",
          count=width,
          offset=scanline_position,
      ).astype(np.float32)
      scanline_position += byte_count
    if scanline_position != block_offset + 8 + data_size:
      raise ValueError(f"{path}: scanline block size mismatch")

  zeros = np.zeros((height, width), dtype=np.float32)
  ones = np.ones((height, width), dtype=np.float32)
  return np.stack(
      [
          planes.get("R", zeros),
          planes.get("G", zeros),
          planes.get("B", zeros),
          planes.get("A", ones),
      ],
      axis=-1,
  )


def to_rgba8(image: np.ndarray, exposure: float) -> np.ndarray:
  rgb = np.maximum(image[..., :3], 0.0)
  rgb = 1.0 - np.exp(-rgb * exposure)
  alpha = np.clip(image[..., 3:4], 0.0, 1.0)
  rgba = np.concatenate([np.clip(rgb, 0.0, 1.0), alpha], axis=-1)
  return np.round(rgba * 255.0).astype(np.uint8)


def bake(sequence_dir: Path, prefix: str, output_name: str, exposure: float) -> None:
  atlas = np.zeros((ATLAS_SIZE, ATLAS_SIZE, 4), dtype=np.uint8)
  for index, frame in enumerate(FRAME_NUMBERS):
    frame_path = sequence_dir / f"{prefix}{frame:03d}.exr"
    rgba = to_rgba8(read_exr_rgba(frame_path), exposure)
    row = index // COLUMNS
    column = index % COLUMNS
    x = BORDER + column * (FRAME_SIZE + SPACING)
    y = BORDER + row * (FRAME_SIZE + SPACING)
    atlas[y:y + FRAME_SIZE, x:x + FRAME_SIZE, :] = rgba

  out_path = TEXTURE_DIR / output_name
  Image.fromarray(atlas, mode="RGBA").save(out_path, optimize=True)
  print(f"Wrote {out_path.relative_to(ROOT)} {ATLAS_SIZE}x{ATLAS_SIZE}")


def main() -> None:
  bake(
      ROOT / "source" / "Explosion00-sequence-exr",
      "explosion00-frame",
      "explosion00_flipbook_exr.png",
      1.0,
  )
  bake(
      ROOT / "source" / "Explosion01-light-nofire-sequence-exr",
      "explosion01-light-nofire-frame",
      "explosion01_smoke_flipbook_exr.png",
      1.0,
  )


if __name__ == "__main__":
  main()
