#!/usr/bin/env python3
"""Inspect one or more thermal .bin samples with numeric stats and preview PNGs."""

from __future__ import annotations

import argparse
import random
from pathlib import Path

import numpy as np

import _bootstrap

_bootstrap.setup_python_path()

from src.common import (
    describe_frame_normalization,
    ensure_dir,
    get_class_names,
    load_dataset_config,
    load_temp14_frame,
    resolve_workspace_path,
    scan_processed_split,
    temp14_preview_u8,
)


def save_preview_png(frame_u16: np.ndarray, config: dict, output_path: Path) -> None:
    """Save a single grayscale preview PNG using the current model normalization."""
    try:
        from PIL import Image
    except ImportError as exc:
        raise SystemExit(
            "Pillow is required only when saving preview PNGs. "
            f"Install it in the active Python environment first: {exc}"
        ) from exc

    preview_u8 = temp14_preview_u8(frame_u16, config)
    image = Image.fromarray(preview_u8, mode="L")
    image = image.resize((preview_u8.shape[1] * 2, preview_u8.shape[0] * 2), Image.NEAREST)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    image.save(output_path)


def print_frame_stats(bin_path: Path, frame_u16: np.ndarray, config: dict) -> None:
    """Print numeric stats for one temp14 sample."""
    temp_c = frame_u16.astype(np.float32) / 16.0 - 273.15
    center_temp_c = float(temp_c[temp_c.shape[0] // 2, temp_c.shape[1] // 2])
    normalization = describe_frame_normalization(frame_u16, config)
    line = (
        f"{bin_path} shape={frame_u16.shape[1]}x{frame_u16.shape[0]} "
        f"min_c={float(np.min(temp_c)):.2f} max_c={float(np.max(temp_c)):.2f} "
        f"mean_c={float(np.mean(temp_c)):.2f} std_c={float(np.std(temp_c)):.2f} "
        f"center_c={center_temp_c:.2f} norm_mode={normalization['mode']}"
    )
    if normalization["mode"] == "delta_from_frame_percentile_clip":
        line += (
            f" bg_p={float(normalization['background_percentile']):.1f} "
            f"bg_c={float(normalization['background_temp_c']):.2f} "
            f"delta_min_c={float(normalization['delta_min_c']):.2f} "
            f"delta_max_c={float(normalization['delta_max_c']):.2f}"
        )
    print(line)


def build_arg_parser() -> argparse.ArgumentParser:
    """Build the CLI argument parser."""
    parser = argparse.ArgumentParser(description="Inspect thermal .bin samples and save preview PNGs")
    parser.add_argument("--config", type=Path, default=None, help="Path to dataset_config.json")
    parser.add_argument("--input", type=Path, default=None, help="Inspect one direct .bin file")
    parser.add_argument("--processed-root", type=Path, default=None, help="Processed dataset root, default from config")
    parser.add_argument("--split", choices=("train", "val", "test"), default="train", help="Processed split to sample from")
    parser.add_argument("--class-name", default=None, help="Optional class filter when sampling from processed data")
    parser.add_argument("--count", type=int, default=8, help="Number of random processed samples to inspect")
    parser.add_argument("--seed", type=int, default=42, help="Sampling seed")
    parser.add_argument("--save-dir", type=Path, default=None, help="Optional directory for preview PNGs")
    return parser


def main() -> int:
    """Program entry point."""
    args = build_arg_parser().parse_args()
    config = load_dataset_config(args.config)

    if args.input is not None:
        frame_u16 = load_temp14_frame(args.input, config)
        print_frame_stats(args.input, frame_u16, config)
        if args.save_dir is not None:
            ensure_dir(args.save_dir)
            preview_path = args.save_dir / f"{args.input.stem}.png"
            save_preview_png(frame_u16, config, preview_path)
            print(f"preview saved to {preview_path}")
        return 0

    processed_root = (
        args.processed_root
        if args.processed_root is not None
        else resolve_workspace_path(config["dataset_paths"]["processed_root"])
    )
    class_names = get_class_names(config)
    samples = scan_processed_split(processed_root, args.split, class_names)
    if args.class_name is not None:
        samples = [sample for sample in samples if sample.class_name == args.class_name]

    if not samples:
        raise SystemExit("no samples found for the requested split/class")

    rng = random.Random(args.seed)
    selected = rng.sample(samples, min(args.count, len(samples)))
    if args.save_dir is not None:
        ensure_dir(args.save_dir)

    for sample in selected:
        frame_u16 = load_temp14_frame(sample.bin_path, config)
        print_frame_stats(sample.bin_path, frame_u16, config)
        if args.save_dir is not None:
            preview_name = f"{sample.class_name}_{sample.bin_path.stem}.png"
            save_preview_png(frame_u16, config, args.save_dir / preview_name)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
