#!/usr/bin/env python3
"""Render TFLite detector predictions and ground-truth boxes onto thermal previews."""

from __future__ import annotations

import argparse
import math
import random
from pathlib import Path
from typing import Any

import numpy as np

import _bootstrap

_bootstrap.setup_python_path()

from src.common import (
    annotation_to_detections,
    decode_detection_map,
    detection_iou,
    ensure_dir,
    get_class_names,
    load_dataset_config,
    load_sidecar_annotation,
    load_temp14_frame,
    normalize_sidecar_annotation,
    normalize_temp14_frame,
    resolve_workspace_path,
    save_json,
    scan_processed_split,
    temp14_preview_u8,
)
from src.tflite_utils import load_tflite_interpreter, run_tflite_inference


CLASS_DISPLAY_NAMES = {
    "circuit_board_normal": "pcb_normal",
    "circuit_board_abnormal_hotspot": "pcb_hotspot",
}


def compute_match_count(ground_truth, predictions, iou_threshold: float) -> int:
    """Compute a simple per-image greedy match count for display."""
    matched_prediction_indexes: set[int] = set()
    match_count = 0

    for gt_box in ground_truth:
        best_index = None
        best_iou = 0.0
        for prediction_index, prediction in enumerate(predictions):
            if prediction_index in matched_prediction_indexes:
                continue
            if prediction.class_name != gt_box.class_name:
                continue
            iou_value = detection_iou(gt_box, prediction)
            if iou_value >= iou_threshold and iou_value > best_iou:
                best_iou = iou_value
                best_index = prediction_index

        if best_index is not None:
            matched_prediction_indexes.add(best_index)
            match_count += 1

    return match_count


def draw_box(
    draw,
    box,
    scale: int,
    y_offset: int,
    color: tuple[int, int, int],
    label: str,
    line_width: int,
) -> None:
    """Draw one scaled box with a text label."""
    x_min = int(round(float(box.x_min) * scale))
    y_min = int(round(float(box.y_min) * scale)) + y_offset
    x_max = int(round(float(box.x_max) * scale))
    y_max = int(round(float(box.y_max) * scale)) + y_offset

    draw.rectangle((x_min, y_min, x_max, y_max), outline=color, width=line_width)
    text_box = (x_min, max(y_min - 14, 0), x_min + max(48, 7 * len(label)), max(y_min, 14))
    draw.rectangle(text_box, fill=color)
    draw.text((text_box[0] + 2, text_box[1] + 1), label, fill=(0, 0, 0))


def build_overlay_image(
    sample,
    interpreter,
    config: dict[str, Any],
    scale: int,
    iou_threshold: float,
):
    """Build one overlay image plus a small JSON-safe summary."""
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError as exc:
        raise SystemExit(
            "Pillow is required for prediction visualization. "
            f"Install it in the active Python environment first: {exc}"
        ) from exc

    frame_u16 = load_temp14_frame(sample.bin_path, config)
    frame_input = normalize_temp14_frame(frame_u16, config, add_channel_axis=True)
    prediction_map = run_tflite_inference(interpreter, frame_input)[0][0]
    predictions = decode_detection_map(prediction_map, config)
    annotation = normalize_sidecar_annotation(load_sidecar_annotation(sample.bin_path))
    ground_truth = annotation_to_detections(annotation, config)
    preview_u8 = temp14_preview_u8(frame_u16, config)

    tile_width = preview_u8.shape[1] * scale
    tile_height = preview_u8.shape[0] * scale
    banner_height = 58
    footer_height = 18
    image = Image.new("RGB", (tile_width, tile_height + banner_height + footer_height), color=(18, 18, 18))
    preview_image = Image.fromarray(preview_u8, mode="L").resize((tile_width, tile_height), Image.NEAREST).convert("RGB")
    image.paste(preview_image, (0, banner_height))

    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default()
    draw.rectangle((0, 0, tile_width, banner_height), fill=(26, 26, 26))

    match_count = compute_match_count(ground_truth, predictions, iou_threshold)
    title_line = f"{sample.class_name} | {sample.bin_path.stem}"
    stats_line = (
        f"gt={len(ground_truth)} pred={len(predictions)} matched={match_count} "
        f"| GT=green Pred=red"
    )
    draw.text((6, 6), title_line, fill=(240, 240, 240), font=font)
    draw.text((6, 24), stats_line, fill=(220, 220, 220), font=font)

    overlay_draw = ImageDraw.Draw(image)
    line_width = max(1, scale // 2)

    for gt_box in ground_truth:
        class_label = CLASS_DISPLAY_NAMES.get(gt_box.class_name, gt_box.class_name)
        draw_box(
            overlay_draw,
            gt_box,
            scale,
            banner_height,
            color=(60, 220, 120),
            label=f"GT {class_label}",
            line_width=line_width,
        )

    for prediction in predictions:
        class_label = CLASS_DISPLAY_NAMES.get(prediction.class_name, prediction.class_name)
        draw_box(
            overlay_draw,
            prediction,
            scale,
            banner_height,
            color=(255, 96, 96),
            label=f"P {class_label} {prediction.score:.2f}",
            line_width=line_width,
        )

    temp_c = frame_u16.astype(np.float32) / 16.0 - 273.15
    footer_text = f"min={float(np.min(temp_c)):.1f}C max={float(np.max(temp_c)):.1f}C center={float(temp_c[temp_c.shape[0] // 2, temp_c.shape[1] // 2]):.1f}C"
    draw.rectangle((0, banner_height + tile_height, tile_width, banner_height + tile_height + footer_height), fill=(26, 26, 26))
    draw.text((6, banner_height + tile_height + 2), footer_text, fill=(220, 220, 220), font=font)

    summary = {
        "sample": str(sample.bin_path),
        "class_name": sample.class_name,
        "ground_truth_count": len(ground_truth),
        "prediction_count": len(predictions),
        "matched_count": match_count,
        "predictions": [
            {
                "class_name": prediction.class_name,
                "score": float(prediction.score),
                "x_min": float(prediction.x_min),
                "y_min": float(prediction.y_min),
                "x_max": float(prediction.x_max),
                "y_max": float(prediction.y_max),
            }
            for prediction in predictions
        ],
    }
    return image, summary


def build_contact_sheet(images, output_path: Path, columns: int) -> None:
    """Pack rendered overlay images into one contact sheet."""
    try:
        from PIL import Image
    except ImportError as exc:
        raise SystemExit(
            "Pillow is required for prediction visualization. "
            f"Install it in the active Python environment first: {exc}"
        ) from exc

    if not images:
        raise SystemExit("no overlay images were generated")

    columns = max(1, min(columns, len(images)))
    tile_width, tile_height = images[0].size
    rows = int(math.ceil(len(images) / columns))
    sheet = Image.new("RGB", (columns * tile_width, rows * tile_height), color=(12, 12, 12))

    for index, image in enumerate(images):
        row_index = index // columns
        column_index = index % columns
        sheet.paste(image, (column_index * tile_width, row_index * tile_height))

    output_path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output_path)


def build_arg_parser() -> argparse.ArgumentParser:
    """Build the CLI argument parser."""
    parser = argparse.ArgumentParser(description="Visualize TFLite detector predictions on processed thermal samples")
    parser.add_argument("--config", type=Path, default=None, help="Path to dataset_config.json")
    parser.add_argument("--processed-root", type=Path, default=None, help="Processed dataset root, default from config")
    parser.add_argument("--model", type=Path, default=None, help="TFLite model path, default best_model_int8.tflite")
    parser.add_argument("--split", choices=("train", "val", "test"), default="test", help="Processed split to visualize")
    parser.add_argument("--class-name", default=None, help="Optional class filter")
    parser.add_argument("--count", type=int, default=12, help="How many samples to render")
    parser.add_argument("--seed", type=int, default=42, help="Sampling seed")
    parser.add_argument("--scale", type=int, default=3, help="Preview upscale factor")
    parser.add_argument("--columns", type=int, default=3, help="Contact-sheet columns")
    parser.add_argument("--output-dir", type=Path, default=None, help="Output directory for overlay PNGs and summary")
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
    model_path = (
        args.model
        if args.model is not None
        else resolve_workspace_path(config["artifacts"]["model_dir"]) / "best_model_int8.tflite"
    )
    output_dir = (
        args.output_dir
        if args.output_dir is not None
        else resolve_workspace_path(config["artifacts"]["report_dir"]) / "visualization" / f"{model_path.stem}_{args.split}"
    )

    if not model_path.exists():
        raise SystemExit(f"TFLite model not found: {model_path}")

    samples = scan_processed_split(processed_root, args.split, class_names)
    if args.class_name is not None:
        samples = [sample for sample in samples if sample.class_name == args.class_name]
    if not samples:
        raise SystemExit("no samples found for the requested split/class")

    rng = random.Random(args.seed)
    selected = rng.sample(samples, min(args.count, len(samples)))
    interpreter = load_tflite_interpreter(model_path)
    ensure_dir(output_dir)

    images = []
    summaries = []
    iou_threshold = float(config["detector"]["eval_iou_threshold"])
    for index, sample in enumerate(selected, start=1):
        overlay_image, summary = build_overlay_image(sample, interpreter, config, max(int(args.scale), 1), iou_threshold)
        relative_name = sample.bin_path.relative_to(processed_root).with_suffix("")
        safe_name = str(relative_name).replace("\\", "__").replace("/", "__")
        output_path = output_dir / f"{index:02d}_{safe_name}.png"
        overlay_image.save(output_path)
        images.append(overlay_image)
        summary["overlay_path"] = str(output_path)
        summaries.append(summary)

    contact_sheet_path = output_dir / "contact_sheet.png"
    build_contact_sheet(images, contact_sheet_path, args.columns)
    save_json(output_dir / "summary.json", {"model": str(model_path), "split": args.split, "samples": summaries})

    print(f"overlay images written to {output_dir}")
    print(f"contact sheet written to {contact_sheet_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
