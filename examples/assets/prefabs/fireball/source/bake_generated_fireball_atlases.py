#!/usr/bin/env python3
"""Bake OpenAI-generated fireball source sheets into runtime atlases.

The renderer consumes RGBA8 textures. The EXR files exported here are compact
source/reference sequences that preserve the authored timing frames in a
higher-range format for future rebakes.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import shutil
import struct

import numpy as np
from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIR = ROOT / "source"
TEXTURE_DIR = ROOT / "textures"
COLUMNS = 4
ROWS = 2
FRAME_SIZE = 256
ATLAS_SIZE = (COLUMNS * FRAME_SIZE, ROWS * FRAME_SIZE)


@dataclass(frozen=True)
class AtlasBake:
  source: str
  atlas: str
  alpha_mode: str
  exposure: float
  exr_dir: str | None = None
  exr_prefix: str | None = None


BAKES = [
    AtlasBake(
        source="openai_fireball_core_flipbook_source.png",
        atlas="fireball_core_flipbook_atlas.png",
        alpha_mode="fire_core",
        exposure=3.0,
        exr_dir="openai_fireball_core_sequence_exr",
        exr_prefix="fireball_core_frame",
    ),
    AtlasBake(
        source="openai_fireball_smoke_flipbook_source.png",
        atlas="fireball_smoke_flipbook_atlas.png",
        alpha_mode="smoke",
        exposure=1.55,
        exr_dir="openai_fireball_smoke_sequence_exr",
        exr_prefix="fireball_smoke_frame",
    ),
    AtlasBake(
        source="openai_fireball_smoke_plumes_source.png",
        atlas="fireball_smoke_plumes_atlas.png",
        alpha_mode="smoke",
        exposure=1.35,
    ),
    AtlasBake(
        source="openai_fireball_ember_burst_source.png",
        atlas="fireball_ember_burst_atlas.png",
        alpha_mode="ember",
        exposure=2.4,
    ),
    AtlasBake(
        source="openai_fireball_lobes_source.png",
        atlas="fireball_flame_lobes_atlas.png",
        alpha_mode="fire",
        exposure=2.2,
    ),
]


def black_to_alpha(rgb: np.ndarray, mode: str) -> np.ndarray:
  rgbf = rgb.astype(np.float32) / 255.0
  maximum = np.max(rgbf, axis=2)
  luma = rgbf[..., 0] * 0.2126 + rgbf[..., 1] * 0.7152 + rgbf[..., 2] * 0.0722
  if mode == "smoke":
    alpha = (np.maximum(maximum, luma * 1.35) - 0.015) / 0.50
    alpha = np.power(np.clip(alpha, 0.0, 1.0), 0.78) * 0.86
  else:
    white_hot = np.where(maximum > 0.72, np.min(rgbf, axis=2), 0.0)
    warm_fire = np.maximum(rgbf[..., 0], rgbf[..., 1] * 0.90) - rgbf[..., 2] * 0.80
    fire_signal = np.maximum(white_hot, warm_fire)
    if mode == "ember":
      threshold = 0.035
      scale = 0.70
      exponent = 0.42
    else:
      threshold = 0.170 if mode == "fire_core" else 0.060
      scale = 0.78 if mode == "fire_core" else 0.88
      exponent = 0.70 if mode == "fire_core" else 0.58
    alpha = (fire_signal - threshold) / scale
    alpha = np.power(np.clip(alpha, 0.0, 1.0), exponent)
  alpha[maximum < 0.012] = 0.0
  return np.round(np.clip(alpha, 0.0, 1.0) * 255.0).astype(np.uint8)


def bake_atlas(source_path: Path, mode: str) -> np.ndarray:
  image = Image.open(source_path).convert("RGB")
  image = image.resize(ATLAS_SIZE, Image.Resampling.LANCZOS)
  rgb = np.asarray(image, dtype=np.uint8)
  alpha = black_to_alpha(rgb, mode)
  if mode != "smoke":
    scale = np.power((alpha.astype(np.float32) / 255.0)[..., None], 1.30)
    rgb = np.round(rgb.astype(np.float32) * scale).astype(np.uint8)
    cutoff = 26 if mode == "fire_core" else 10
    rgb[alpha < cutoff] = 0
  rgba = np.dstack([rgb, alpha])
  rgba[alpha == 0] = 0
  return rgba


def exr_attr(name: str, attr_type: str, value: bytes) -> bytes:
  return (
      name.encode("latin1") + b"\0" +
      attr_type.encode("latin1") + b"\0" +
      struct.pack("<I", len(value)) +
      value
  )


def exr_channels(names: list[str]) -> bytes:
  out = bytearray()
  for name in names:
    out += name.encode("latin1") + b"\0"
    out += struct.pack("<iBBBBii", 1, 0, 0, 0, 0, 1, 1)
  out += b"\0"
  return bytes(out)


def write_exr_rgba(path: Path, rgba8: np.ndarray, exposure: float) -> None:
  height, width, _channels = rgba8.shape
  rgb = np.power(rgba8[..., :3].astype(np.float32) / 255.0, 2.2) * exposure
  alpha = rgba8[..., 3].astype(np.float32) / 255.0
  planes = {
      "R": rgb[..., 0].astype("<f2"),
      "G": rgb[..., 1].astype("<f2"),
      "B": rgb[..., 2].astype("<f2"),
      "A": alpha.astype("<f2"),
  }

  header = bytearray(struct.pack("<II", 20000630, 2))
  header += exr_attr("channels", "chlist", exr_channels(["R", "G", "B", "A"]))
  header += exr_attr("compression", "compression", b"\0")
  window = struct.pack("<iiii", 0, 0, width - 1, height - 1)
  header += exr_attr("dataWindow", "box2i", window)
  header += exr_attr("displayWindow", "box2i", window)
  header += exr_attr("lineOrder", "lineOrder", b"\0")
  header += exr_attr("pixelAspectRatio", "float", struct.pack("<f", 1.0))
  header += exr_attr("screenWindowCenter", "v2f", struct.pack("<ff", 0.0, 0.0))
  header += exr_attr("screenWindowWidth", "float", struct.pack("<f", 1.0))
  header += b"\0"

  offset_table_size = height * 8
  blocks: list[bytes] = []
  offsets: list[int] = []
  position = len(header) + offset_table_size
  for y in range(height):
    payload = b"".join(planes[channel][y, :].tobytes() for channel in ["R", "G", "B", "A"])
    block = struct.pack("<iI", y, len(payload)) + payload
    blocks.append(block)
    offsets.append(position)
    position += len(block)

  path.parent.mkdir(parents=True, exist_ok=True)
  with path.open("wb") as stream:
    stream.write(header)
    for offset in offsets:
      stream.write(struct.pack("<Q", offset))
    for block in blocks:
      stream.write(block)


def write_sequence(rgba: np.ndarray, bake: AtlasBake) -> None:
  if bake.exr_dir is None or bake.exr_prefix is None:
    return
  out_dir = SOURCE_DIR / bake.exr_dir
  if out_dir.exists():
    shutil.rmtree(out_dir)
  out_dir.mkdir(parents=True)
  for index in range(COLUMNS * ROWS):
    row = index // COLUMNS
    column = index % COLUMNS
    x = column * FRAME_SIZE
    y = row * FRAME_SIZE
    frame = rgba[y:y + FRAME_SIZE, x:x + FRAME_SIZE, :]
    write_exr_rgba(out_dir / f"{bake.exr_prefix}{index + 1:03d}.exr", frame, bake.exposure)


def main() -> None:
  TEXTURE_DIR.mkdir(parents=True, exist_ok=True)
  for bake in BAKES:
    source_path = SOURCE_DIR / bake.source
    if not source_path.exists():
      raise FileNotFoundError(source_path)
    atlas = bake_atlas(source_path, bake.alpha_mode)
    Image.fromarray(atlas, mode="RGBA").save(TEXTURE_DIR / bake.atlas, optimize=True)
    write_sequence(atlas, bake)
    print(f"Wrote textures/{bake.atlas} {ATLAS_SIZE[0]}x{ATLAS_SIZE[1]}")


if __name__ == "__main__":
  main()
