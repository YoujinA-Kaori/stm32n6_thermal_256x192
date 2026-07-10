#!/usr/bin/env python3
"""Create a contact-sheet PNG for random thermal samples."""

from __future__ import annotations

import argparse
import math
import random
from pathlib import Path

import numpy as np

import _bootstrap

_bootstrap.setup_python_path()

from src.common import (
    get_class_names,
    load_dataset_config,
    load_temp14_frame,
    resolve_workspace_path,
    scan_processed_split,
    temp14_preview_u8,
)


def build_contact_sheet(
    sample_paths: list[tuple[str, Path]],
    config: dict,
    output_path: Path,
) -> None:
    """Render a labeled contact sheet from processed thermal samples."""
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError as exc:
        raise SystemExit(
            "Pillow is required for contact-sheet rendering. "
            f"Install it in the active Python environment first: {exc}"
        ) from exc

    scale = 2
    tile_width = int(config["input"]["width"]) * scale
    tile_height = int(config["input"]["height"]) * scale
    label_height = 40
    columns = min(4, max(1, len(sample_paths)))
    rows = int(math.ceil(len(sample_paths) / columns))
    sheet = Image.new("RGB", (columns * tile_width, rows * (tile_height + label_height)), color=(18, 18, 18))
    font = ImageFont.load_default()

    for index, (class_name, bin_path) in enumerate(sample_paths):
        frame_u16 = load_temp14_frame(bin_path, config)
        temp_c = frame_u16.astype(np.float32) / 16.0 - 273.15
        preview_u8 = temp14_preview_u8(frame_u16, config)
        tile_image = Image.fromarray(preview_u8, mode="L").resize((tile_width, tile_height), Image.NEAREST).convert("RGB")

        row_index = index // columns
        column_index = index % columns
        x0 = column_index * tile_width
        y0 = row_index * (tile_height + label_height)
        sheet.paste(tile_image, (x0, y0))

        draw = ImageDraw.Draw(sheet)
        label = (
            f"{class_name}\n"
            f"{bin_path.stem} min={float(np.min(temp_c)):.1f}C max={float(np.max(temp_c)):.1f}C"
        )
        draw.rectangle((x0, y0 + tile_height, x0 + tile_width, y0 + tile_height + label_height), fill=(28, 28, 28))
        draw.text((x0 + 8, y0 + tile_height + 6), label, fill=(240, 240, 240), font=font)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output_path)


def build_arg_parser() -> argparse.ArgumentParser:
    """Build the CLI argument parser."""
    parser = argparse.ArgumentParser(description="Render a random thermal dataset contact sheet")
    parser.add_argument("--config", type=Path, default=None, help="Path to dataset_config.json")
    parser.add_argument("--processed-root", type=Path, default=None, help="Processed dataset root, default from config")
    parser.add_argument("--split", choices=("train", "val", "test"), default="train", help="Processed split to visualize")
    parser.add_argument("--class-name", default=None, help="Optional class filter")
    parser.add_argument("--count", type=int, default=12, help="Number of random samples to draw")
    parser.add_argument("--seed", type=int, default=42, help="Sampling seed")
    parser.add_argument("--output", type=Path, required=True, help="Output PNG path")
    return parser


def main() -> int:
    """Program entry point."""
    args = build_arg_parser().parse_args()
    config = load_dataset_config(args.config)
    class_names = get_class_names(config)
    processed_root = (
        args.processed_root
        if args.processed_root is not None
        else resolve_workspace_path(config["dataset_paths"]["processed_root"])
    )

    samples = scan_processed_split(processed_root, args.split, class_names)
    if args.class_name is not None:
        samples = [sample for sample in samples if sample.class_name == args.class_name]
    if not samples:
        raise SystemExit("no samples found for the requested split/class")

    rng = random.Random(args.seed)
    selected = rng.sample(samples, min(args.count, len(samples)))
    build_contact_sheet([(sample.class_name, sample.bin_path) for sample in selected], config, args.output)
    print(f"contact sheet written to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
