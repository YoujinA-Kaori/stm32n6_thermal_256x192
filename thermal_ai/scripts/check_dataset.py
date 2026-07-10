#!/usr/bin/env python3
"""Validate processed thermal datasets before detection training."""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path
from typing import Any

import numpy as np

import _bootstrap

_bootstrap.setup_python_path()

from src.common import (
    count_detection_target_collisions,
    get_class_names,
    get_detection_class_names,
    load_dataset_config,
    load_sidecar_annotation,
    load_temp14_frame,
    make_json_safe,
    normalize_sidecar_annotation,
    resolve_workspace_path,
    save_json,
    scan_processed_split,
)


def inspect_split(
    processed_root: Path,
    split_name: str,
    class_names: list[str],
    config: dict[str, Any],
) -> dict[str, Any]:
    """Inspect one processed split and return aggregated statistics."""
    stats: dict[str, Any] = {"classes": {}, "total_files": 0, "errors": []}
    split_samples = scan_processed_split(processed_root, split_name, class_names)
    stats["total_files"] = len(split_samples)
    frame_height = int(config["input"]["height"])
    frame_width = int(config["input"]["width"])
    detection_class_names = set(get_detection_class_names(config))

    grouped_samples: dict[str, list[Any]] = defaultdict(list)
    for sample in split_samples:
        grouped_samples[sample.class_name].append(sample)

    for class_name in class_names:
        class_stats = {
            "files": 0,
            "groups": 0,
            "annotations": 0,
            "missing_annotations": 0,
            "annotated_objects": 0,
            "detector_cell_collisions": 0,
            "min_temp_c": None,
            "max_temp_c": None,
            "mean_center_temp_c": None,
        }
        center_temps: list[float] = []
        samples = grouped_samples.get(class_name, [])
        class_stats["files"] = len(samples)
        class_stats["groups"] = len({sample.group_key for sample in samples})

        min_temp_c = None
        max_temp_c = None

        for sample in samples:
            try:
                frame_u16 = load_temp14_frame(sample.bin_path, config)
            except Exception as exc:
                stats["errors"].append(f"{sample.bin_path}: {exc}")
                continue

            annotation = normalize_sidecar_annotation(load_sidecar_annotation(sample.bin_path))
            if annotation is None or annotation.is_empty or not annotation.objects:
                class_stats["missing_annotations"] += 1
                stats["errors"].append(f"{sample.bin_path}: missing bbox annotation for detector sample")
            else:
                object_class_names = {annotated_object.class_name for annotated_object in annotation.objects}
                if sample.class_name not in object_class_names:
                    stats["errors"].append(
                        f"{sample.bin_path}: primary folder class not present in annotation objects "
                        f"(sample={sample.class_name}, objects={sorted(object_class_names)})"
                    )
                if annotation.primary_class_name is not None and annotation.primary_class_name != sample.class_name:
                    stats["errors"].append(
                        f"{sample.bin_path}: primary_class_name mismatch "
                        f"(sample={sample.class_name}, annotation={annotation.primary_class_name})"
                    )
                unknown_classes = sorted(object_class_names - detection_class_names)
                if unknown_classes:
                    stats["errors"].append(f"{sample.bin_path}: unknown annotation classes {unknown_classes}")

                for annotated_object in annotation.objects:
                    if not (0.0 <= annotated_object.x_min < annotated_object.x_max <= float(frame_width)):
                        stats["errors"].append(f"{sample.bin_path}: invalid bbox x-range {annotated_object}")
                    if not (0.0 <= annotated_object.y_min < annotated_object.y_max <= float(frame_height)):
                        stats["errors"].append(f"{sample.bin_path}: invalid bbox y-range {annotated_object}")

                class_stats["annotations"] += 1
                class_stats["annotated_objects"] += len(annotation.objects)
                class_stats["detector_cell_collisions"] += count_detection_target_collisions(annotation, config)

            temp_c = frame_u16.astype(np.float32) / 16.0 - 273.15
            frame_min = float(np.min(temp_c))
            frame_max = float(np.max(temp_c))
            center_temp = float(temp_c[temp_c.shape[0] // 2, temp_c.shape[1] // 2])

            min_temp_c = frame_min if min_temp_c is None else min(min_temp_c, frame_min)
            max_temp_c = frame_max if max_temp_c is None else max(max_temp_c, frame_max)
            center_temps.append(center_temp)

        class_stats["min_temp_c"] = min_temp_c
        class_stats["max_temp_c"] = max_temp_c
        class_stats["mean_center_temp_c"] = float(np.mean(center_temps)) if center_temps else None
        stats["classes"][class_name] = class_stats

    return stats


def build_arg_parser() -> argparse.ArgumentParser:
    """Build the CLI argument parser."""
    parser = argparse.ArgumentParser(description="Validate processed thermal dataset files")
    parser.add_argument("--config", type=Path, default=None, help="Path to dataset_config.json")
    parser.add_argument("--processed-root", type=Path, default=None, help="Processed dataset root, default from config")
    parser.add_argument(
        "--split",
        choices=("train", "val", "test", "all"),
        default="all",
        help="Which split to inspect",
    )
    parser.add_argument("--json-out", type=Path, default=None, help="Optional JSON report output path")
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

    if not processed_root.exists():
        raise SystemExit(f"processed dataset root does not exist: {processed_root}")

    split_names = ["train", "val", "test"] if args.split == "all" else [args.split]
    report: dict[str, Any] = {}

    for split_name in split_names:
        split_report = inspect_split(processed_root, split_name, class_names, config)
        report[split_name] = split_report
        print(f"[{split_name}] total_files={split_report['total_files']} errors={len(split_report['errors'])}")
        for class_name in class_names:
            class_stats = split_report["classes"][class_name]
            print(
                f"  {class_name}: files={class_stats['files']} groups={class_stats['groups']} "
                f"annotations={class_stats['annotations']} missing_ann={class_stats['missing_annotations']} "
                f"objects={class_stats['annotated_objects']} collisions={class_stats['detector_cell_collisions']} "
                f"min_c={class_stats['min_temp_c']} max_c={class_stats['max_temp_c']} "
                f"center_mean_c={class_stats['mean_center_temp_c']}"
            )
        for error_line in split_report["errors"]:
            print(f"  error: {error_line}")

    if args.json_out is not None:
        save_json(args.json_out, make_json_safe(report))
        print(f"json report written to {args.json_out}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
