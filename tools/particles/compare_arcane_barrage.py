#!/usr/bin/env python3
"""Capture and compare the generated arcane barrage particle preset."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path
from urllib.parse import quote


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def find_first_existing(candidates: list[Path]) -> Path | None:
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def find_chromium(explicit: str | None) -> str:
    if explicit:
        return explicit
    for name in ("chromium", "chromium-browser", "google-chrome", "google-chrome-stable"):
        resolved = shutil.which(name)
        if resolved:
            return resolved
    raise RuntimeError("could not find chromium, chromium-browser, or google-chrome")


def find_preview_binary(repo_root: Path, explicit: str | None) -> Path:
    if explicit:
        path = Path(explicit)
        if path.exists():
            return path
        raise RuntimeError(f"preview binary does not exist: {path}")

    candidates = [
        repo_root / "build" / "examples" / "particles" / "generated_preview",
        repo_root / "build" / "portable" / "examples" / "particles" / "generated_preview",
        repo_root / "build" / "minimal-headless" / "examples" / "particles" / "generated_preview",
        repo_root / "build" / "particles_generated_preview",
        repo_root / "build" / "portable" / "particles_generated_preview",
        repo_root / "build" / "minimal-headless" / "particles_generated_preview",
    ]
    found = find_first_existing(candidates)
    if found:
        return found

    matches = sorted((repo_root / "build").rglob("particles_generated_preview"))
    if matches:
        return matches[0]
    raise RuntimeError("could not find particles_generated_preview under build/")


def default_reference(repo_root: Path) -> Path | None:
    return find_first_existing(
        [
            repo_root / "reference.jpeg",
            repo_root / "reference.jpg",
            repo_root / "reference.avif",
            repo_root / "arcane_barrage.jpeg",
            repo_root / "arcane_barrage.jpg",
            repo_root / "arcane_barrage.avif",
            repo_root / "build" / "reference-captures" / "reference_effect.png",
        ]
    )


def file_uri(path: Path) -> str:
    return "file://" + quote(str(path.resolve()))


def convert_reference_with_chromium(reference: Path, output_dir: Path, chromium: str) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    output = output_dir / "reference_effect.png"
    if reference.suffix.lower() == ".png" and reference.resolve() == output.resolve():
        return output
    html = output_dir / "reference_effect.html"
    html.write_text(
        "\n".join(
            [
                "<!doctype html>",
                "<meta charset=\"utf-8\">",
                "<style>",
                "html, body { margin: 0; width: 100%; height: 100%; background: #050707; }",
                "body { display: grid; place-items: center; overflow: hidden; }",
                "img { max-width: 100vw; max-height: 100vh; object-fit: contain; }",
                "</style>",
                f"<img src=\"{file_uri(reference)}\" alt=\"reference\">",
            ]
        ),
        encoding="utf-8",
    )
    command = [
        chromium,
        "--headless",
        "--disable-gpu",
        "--no-sandbox",
        "--window-size=900,900",
        f"--screenshot={output}",
        file_uri(html),
    ]
    subprocess.run(command, check=True, cwd=output_dir)
    return output


def clean_capture_dir(capture_dir: Path) -> None:
    capture_dir.mkdir(parents=True, exist_ok=True)
    for pattern in ("*.ppm", "*.png"):
        for path in capture_dir.glob(pattern):
            path.unlink()


def run_preview_capture(
    repo_root: Path,
    preview_binary: Path,
    spec_path: Path,
    package_dir: Path,
    capture_dir: Path,
    frames: str,
    timeout: float,
) -> str:
    clean_capture_dir(capture_dir)
    env = os.environ.copy()
    env["KARMA_REPO_ROOT"] = str(repo_root)
    env["KARMA_PARTICLE_STATS"] = "1"
    env["VK_INSTANCE_LAYERS"] = "VK_LAYER_LUNARG_screenshot"
    env["VK_SCREENSHOT_FRAMES"] = frames
    env["VK_SCREENSHOT_DIR"] = str(capture_dir)
    env["VK_SCREENSHOT_FORMAT"] = "UNORM"

    command = [str(preview_binary), str(spec_path), str(package_dir)]
    try:
        completed = subprocess.run(
            command,
            cwd=repo_root,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
            check=False,
        )
        return completed.stdout
    except subprocess.TimeoutExpired as exc:
        output = exc.stdout or ""
        if isinstance(output, bytes):
            output = output.decode("utf-8", errors="replace")
        return output + f"\npreview capture stopped after {timeout:.1f}s timeout\n"


def import_image_tools():
    try:
        from PIL import Image, ImageDraw
        import numpy as np
    except ImportError as exc:
        raise RuntimeError("Pillow and numpy are required: python3 -m pip install pillow numpy") from exc
    return Image, ImageDraw, np


def convert_ppm_captures(capture_dir: Path, Image) -> list[Path]:
    pngs: list[Path] = []
    for ppm in sorted(capture_dir.glob("*.ppm")):
        png = ppm.with_suffix(".png")
        with Image.open(ppm) as image:
            image.save(png)
        pngs.append(png)
    return pngs


def palette_stats(path: Path, Image, np) -> dict[str, object]:
    image = Image.open(path).convert("RGB")
    arr = np.asarray(image, dtype=np.float32) / 255.0
    luma = arr[..., 0] * 0.2126 + arr[..., 1] * 0.7152 + arr[..., 2] * 0.0722
    active = luma > 0.02
    active_arr = arr[active] if active.any() else arr.reshape((-1, 3))
    teal = (
        (active_arr[:, 1] > active_arr[:, 0] * 1.12)
        & (active_arr[:, 2] > active_arr[:, 0] * 1.08)
        & (active_arr[:, 1] > 0.25)
    )
    quantized = image.quantize(colors=6, method=Image.Quantize.MEDIANCUT)
    palette = quantized.getpalette()[: 6 * 3]
    counts = quantized.getcolors()
    colors: list[tuple[int, str]] = []
    if counts:
        for count, index in sorted(counts, reverse=True)[:6]:
            offset = index * 3
            colors.append((count, "#{:02x}{:02x}{:02x}".format(*palette[offset : offset + 3])))
    return {
        "size": image.size,
        "mean_rgb": active_arr.mean(axis=0),
        "p95_luma": float(np.percentile(luma, 95)),
        "p99_luma": float(np.percentile(luma, 99)),
        "max_luma": float(luma.max()),
        "active_pixels_pct": float(active.mean() * 100.0),
        "teal_pixels_pct": float(teal.mean() * 100.0) if active_arr.size else 0.0,
        "palette": colors,
    }


def resize_to_height(image, height: int):
    scale = height / max(image.height, 1)
    width = max(int(image.width * scale), 1)
    return image.resize((width, height))


def contact_sheet(reference_png: Path, preview_png: Path, output: Path, Image, ImageDraw) -> None:
    reference = Image.open(reference_png).convert("RGB")
    preview = Image.open(preview_png).convert("RGB")
    height = min(720, max(reference.height, preview.height))
    reference = resize_to_height(reference, height)
    preview = resize_to_height(preview, height)
    label_height = 28
    gap = 12
    sheet = Image.new(
        "RGB",
        (reference.width + preview.width + gap, height + label_height),
        (5, 7, 7),
    )
    draw = ImageDraw.Draw(sheet)
    draw.text((8, 7), "reference", fill=(230, 250, 248))
    draw.text((reference.width + gap + 8, 7), "generated", fill=(230, 250, 248))
    sheet.paste(reference, (0, label_height))
    sheet.paste(preview, (reference.width + gap, label_height))
    output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output)


def format_stats(label: str, stats: dict[str, object]) -> str:
    mean = stats["mean_rgb"]
    palette = ", ".join(f"{hex_color}:{count}" for count, hex_color in stats["palette"])
    return (
        f"{label}: size={stats['size']} "
        f"mean_rgb=({mean[0]:.3f},{mean[1]:.3f},{mean[2]:.3f}) "
        f"p95_luma={stats['p95_luma']:.3f} p99_luma={stats['p99_luma']:.3f} "
        f"max_luma={stats['max_luma']:.3f} active={stats['active_pixels_pct']:.2f}% "
        f"teal={stats['teal_pixels_pct']:.2f}% palette=[{palette}]"
    )


def parse_args() -> argparse.Namespace:
    repo_root = repo_root_from_script()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, default=default_reference(repo_root))
    parser.add_argument(
        "--spec",
        type=Path,
        default=repo_root / "examples" / "particles" / "specs" / "arcane_barrage.kpspec.json",
    )
    parser.add_argument("--preview-bin")
    parser.add_argument("--chromium")
    parser.add_argument(
        "--package-dir",
        type=Path,
        default=repo_root / "generated" / "arcane_barrage_compare",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=repo_root / "build" / "reference-captures",
    )
    parser.add_argument("--frames", default="45")
    parser.add_argument("--timeout", type=float, default=8.0)
    return parser.parse_args()


def main() -> int:
    repo_root = repo_root_from_script()
    args = parse_args()
    if args.reference is None or not args.reference.exists():
        print("reference image not found; pass --reference <path>", file=sys.stderr)
        return 2
    if not args.spec.exists():
        print(f"spec not found: {args.spec}", file=sys.stderr)
        return 2

    try:
        Image, ImageDraw, np = import_image_tools()
        chromium = find_chromium(args.chromium)
        preview_binary = find_preview_binary(repo_root, args.preview_bin)
        reference_png = convert_reference_with_chromium(args.reference, args.output_dir, chromium)
        capture_dir = args.output_dir / "arcane_barrage_capture"
        log = run_preview_capture(
            repo_root,
            preview_binary,
            args.spec,
            args.package_dir,
            capture_dir,
            args.frames,
            args.timeout,
        )
        preview_pngs = convert_ppm_captures(capture_dir, Image)
        if not preview_pngs:
            print(log)
            raise RuntimeError(f"no .ppm captures were produced in {capture_dir}")
        preview_png = preview_pngs[-1]
        sheet = args.output_dir / "arcane_barrage_comparison.png"
        contact_sheet(reference_png, preview_png, sheet, Image, ImageDraw)

        print(format_stats("reference", palette_stats(reference_png, Image, np)))
        print(format_stats("generated", palette_stats(preview_png, Image, np)))
        stats_lines = [line for line in log.splitlines() if "Particle stats:" in line]
        if stats_lines:
            print(stats_lines[-1])
        print(f"reference: {reference_png}")
        print(f"preview:   {preview_png}")
        print(f"sheet:     {sheet}")
        return 0
    except (RuntimeError, subprocess.CalledProcessError) as exc:
        print(str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
