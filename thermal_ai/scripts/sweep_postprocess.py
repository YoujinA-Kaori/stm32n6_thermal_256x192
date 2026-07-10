#!/usr/bin/env python3
"""Sweep detector post-processing settings and report validation/test metrics."""

from __future__ import annotations

import argparse
import copy
from pathlib import Path
from typing import Any

import numpy as np

import _bootstrap

_bootstrap.setup_python_path()

from src.common import (
    annotation_to_detections,
    decode_detection_map,
    ensure_dir,
    get_class_names,
    get_detection_class_names,
    load_dataset_config,
    load_sidecar_annotation,
    load_temp14_frame,
    make_json_safe,
    normalize_sidecar_annotation,
    normalize_temp14_frame,
    resolve_workspace_path,
    save_json,
    scan_processed_split,
)
from src.metrics import compute_detection_metrics, format_detection_report
from src.tflite_utils import load_tflite_interpreter, run_tflite_inference


def _collect_prediction_maps(interpreter, sample_records, config: dict[str, Any]) -> list[np.ndarray]:
    """Run inference once and keep raw detector maps for fast postprocess sweeps."""
    prediction_maps: list[np.ndarray] = []
    for sample in sample_records:
        frame_u16 = load_temp14_frame(sample.bin_path, config)
        frame_input = normalize_temp14_frame(frame_u16, config, add_channel_axis=True)
        prediction_maps.append(run_tflite_inference(interpreter, frame_input)[0][0])
    return prediction_maps


def _collect_ground_truth(sample_records, config: dict[str, Any]):
    """Load ground-truth detections for one split."""
    ground_truth_batches = []
    for sample in sample_records:
        annotation = normalize_sidecar_annotation(load_sidecar_annotation(sample.bin_path))
        ground_truth_batches.append(annotation_to_detections(annotation, config))
    return ground_truth_batches


def _evaluate_prediction_maps(
    prediction_maps: list[np.ndarray],
    ground_truth_batches,
    config: dict[str, Any],
) -> dict[str, Any]:
    """Decode maps with the supplied config and compute detection metrics."""
    class_names = get_detection_class_names(config)
    iou_threshold = float(config["detector"]["eval_iou_threshold"])
    prediction_batches = [decode_detection_map(prediction_map, config) for prediction_map in prediction_maps]
    return compute_detection_metrics(ground_truth_batches, prediction_batches, class_names, iou_threshold)


def _candidate_configs(base_config: dict[str, Any]):
    """Yield practical postprocess candidates tuned for board + hotspot detection."""
    normal_name = "circuit_board_normal"
    hotspot_name = "circuit_board_abnormal_hotspot"

    normal_score_values = [0.90, 0.92, 0.94, 0.96]
    hotspot_score_values = [0.90, 0.92, 0.94, 0.96, 0.98]
    normal_min_areas = [5000.0, 7000.0, 9000.0, 11000.0]
    hotspot_min_areas = [120.0, 200.0, 300.0, 420.0]
    hotspot_max_areas = [2500.0, 3200.0, 4200.0, 5600.0]

    for normal_score in normal_score_values:
        for hotspot_score in hotspot_score_values:
            for normal_min_area in normal_min_areas:
                for hotspot_min_area in hotspot_min_areas:
                    for hotspot_max_area in hotspot_max_areas:
                        if hotspot_min_area >= hotspot_max_area:
                            continue

                        config = copy.deepcopy(base_config)
                        detector_cfg = config["detector"]
                        detector_cfg["score_thresholds"] = {
                            normal_name: normal_score,
                            hotspot_name: hotspot_score,
                        }
                        detector_cfg["postprocess_filter"] = {
                            "enabled": True,
                            normal_name: {
                                "min_width_px": 60.0,
                                "min_height_px": 40.0,
                                "min_area_px": normal_min_area,
                            },
                            hotspot_name: {
                                "min_width_px": 6.0,
                                "min_height_px": 6.0,
                                "min_area_px": hotspot_min_area,
                                "max_area_px": hotspot_max_area,
                            },
                        }
                        yield {
                            "normal_score": normal_score,
                            "hotspot_score": hotspot_score,
                            "normal_min_area": normal_min_area,
                            "hotspot_min_area": hotspot_min_area,
                            "hotspot_max_area": hotspot_max_area,
                            "config": config,
                        }


def _score_candidate(metrics: dict[str, Any]) -> float:
    """Favor balanced F1, then precision when F1 is close."""
    return (
        float(metrics["macro_f1_score"]) * 1000.0
        + float(metrics["micro_f1_score"]) * 200.0
        + float(metrics["macro_precision"]) * 50.0
    )


def _evaluate_split(interpreter, sample_records, base_config: dict[str, Any]):
    """Collect raw predictions and labels for one split."""
    prediction_maps = _collect_prediction_maps(interpreter, sample_records, base_config)
    ground_truth_batches = _collect_ground_truth(sample_records, base_config)
    return prediction_maps, ground_truth_batches


def build_arg_parser() -> argparse.ArgumentParser:
    """Build the CLI parser."""
    parser = argparse.ArgumentParser(description="Sweep thermal detector post-processing parameters")
    parser.add_argument("--config", type=Path, default=None, help="Path to dataset_config.json")
    parser.add_argument("--processed-root", type=Path, default=None, help="Processed dataset root")
    parser.add_argument("--model", type=Path, default=None, help="TFLite model path")
    parser.add_argument("--search-split", choices=("train", "val", "test"), default="val")
    parser.add_argument("--eval-split", choices=("train", "val", "test"), default="test")
    parser.add_argument("--top-k", type=int, default=8)
    parser.add_argument("--report-dir", type=Path, default=None)
    return parser


def main() -> int:
    """Program entry point."""
    args = build_arg_parser().parse_args()
    base_config = load_dataset_config(args.config)
    class_names = get_class_names(base_config)
    processed_root = (
        args.processed_root
        if args.processed_root is not None
        else resolve_workspace_path(base_config["dataset_paths"]["processed_root"])
    )
    model_path = (
        args.model
        if args.model is not None
        else resolve_workspace_path(base_config["artifacts"]["model_dir"]) / "best_model_int8.tflite"
    )
    report_dir = (
        args.report_dir
        if args.report_dir is not None
        else resolve_workspace_path(base_config["artifacts"]["report_dir"]) / "postprocess_sweep"
    )
    ensure_dir(report_dir)

    interpreter = load_tflite_interpreter(model_path)
    search_samples = scan_processed_split(processed_root, args.search_split, class_names)
    eval_samples = scan_processed_split(processed_root, args.eval_split, class_names)
    if not search_samples:
        raise SystemExit(f"no samples found in split: {args.search_split}")
    if not eval_samples:
        raise SystemExit(f"no samples found in split: {args.eval_split}")

    search_maps, search_ground_truth = _evaluate_split(interpreter, search_samples, base_config)
    eval_maps, eval_ground_truth = _evaluate_split(interpreter, eval_samples, base_config)
    baseline_search = _evaluate_prediction_maps(search_maps, search_ground_truth, base_config)
    baseline_eval = _evaluate_prediction_maps(eval_maps, eval_ground_truth, base_config)

    ranked = []
    for candidate in _candidate_configs(base_config):
        metrics = _evaluate_prediction_maps(search_maps, search_ground_truth, candidate["config"])
        ranked.append(
            {
                "score": _score_candidate(metrics),
                "params": {key: value for key, value in candidate.items() if key != "config"},
                "search_metrics": metrics,
                "config": candidate["config"],
            }
        )

    ranked.sort(key=lambda item: item["score"], reverse=True)
    top_items = ranked[: max(1, args.top_k)]
    best = top_items[0]
    best_eval = _evaluate_prediction_maps(eval_maps, eval_ground_truth, best["config"])
    best["eval_metrics"] = best_eval

    report_payload = {
        "model": str(model_path),
        "search_split": args.search_split,
        "eval_split": args.eval_split,
        "baseline_search_metrics": baseline_search,
        "baseline_eval_metrics": baseline_eval,
        "best_params": best["params"],
        "best_search_metrics": best["search_metrics"],
        "best_eval_metrics": best_eval,
        "top_candidates": [
            {
                "score": item["score"],
                "params": item["params"],
                "search_metrics": item["search_metrics"],
            }
            for item in top_items
        ],
    }
    save_json(report_dir / "postprocess_sweep_summary.json", make_json_safe(report_payload))

    print("baseline eval report:")
    print(format_detection_report(get_detection_class_names(base_config), baseline_eval))
    print("")
    print("best params:")
    for key, value in best["params"].items():
        print(f"{key}: {value}")
    print("")
    print("best eval report:")
    print(format_detection_report(get_detection_class_names(base_config), best_eval))
    print("")
    print(f"summary: {report_dir / 'postprocess_sweep_summary.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
