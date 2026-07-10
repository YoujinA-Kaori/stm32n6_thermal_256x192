#!/usr/bin/env python3
"""Run TFLite detector inference on a processed split and report metrics."""

from __future__ import annotations

import argparse
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
from src.metrics import build_collection_suggestions, compute_detection_metrics, format_detection_report
from src.model import get_custom_objects, require_tensorflow
from src.tflite_utils import inspect_tflite_model, load_tflite_interpreter, run_tflite_inference


def evaluate_tflite(interpreter, sample_records, config: dict[str, Any]) -> dict[str, Any]:
    """Run TFLite inference across one split and compute detection metrics."""
    detection_class_names = get_detection_class_names(config)
    iou_threshold = float(config["detector"]["eval_iou_threshold"])
    ground_truth_batches = []
    prediction_batches = []
    max_detection_scores: list[float] = []

    for sample in sample_records:
        frame_u16 = load_temp14_frame(sample.bin_path, config)
        frame_input = normalize_temp14_frame(frame_u16, config, add_channel_axis=True)
        outputs = run_tflite_inference(interpreter, frame_input)
        prediction_map = outputs[0][0]
        detections = decode_detection_map(prediction_map, config)
        annotation = normalize_sidecar_annotation(load_sidecar_annotation(sample.bin_path))

        ground_truth_batches.append(annotation_to_detections(annotation, config))
        prediction_batches.append(detections)
        max_detection_scores.append(max((detection.score for detection in detections), default=0.0))

    detection_metrics = compute_detection_metrics(ground_truth_batches, prediction_batches, detection_class_names, iou_threshold)
    return {
        "detection_metrics": detection_metrics,
        "mean_confidence": float(np.mean(max_detection_scores)) if max_detection_scores else 0.0,
    }


def compare_with_keras(model, interpreter, sample_records, config: dict[str, Any]) -> dict[str, Any]:
    """Measure Keras/TFLite agreement for raw detector tensors and decoded metrics."""
    keras_prediction_maps = []
    tflite_prediction_maps = []
    mean_abs_deltas: list[float] = []
    max_abs_delta = 0.0

    for sample in sample_records:
        frame_u16 = load_temp14_frame(sample.bin_path, config)
        frame_input = normalize_temp14_frame(frame_u16, config, add_channel_axis=True)
        keras_prediction_map = model.predict(frame_input[np.newaxis, ...], verbose=0)[0]
        tflite_outputs = run_tflite_inference(interpreter, frame_input)
        tflite_prediction_map = tflite_outputs[0][0]

        keras_prediction_maps.append(keras_prediction_map)
        tflite_prediction_maps.append(tflite_prediction_map)
        abs_delta = np.abs(keras_prediction_map - tflite_prediction_map)
        mean_abs_deltas.append(float(np.mean(abs_delta)))
        max_abs_delta = max(max_abs_delta, float(np.max(abs_delta)))

    return {
        "samples": len(sample_records),
        "mean_abs_delta": float(np.mean(mean_abs_deltas)) if mean_abs_deltas else 0.0,
        "max_abs_delta": max_abs_delta,
        "keras_detection_metrics": _evaluate_prediction_maps(keras_prediction_maps, sample_records, config),
        "tflite_detection_metrics": _evaluate_prediction_maps(tflite_prediction_maps, sample_records, config),
    }


def _evaluate_prediction_maps(prediction_maps: list[np.ndarray], sample_records, config: dict[str, Any]) -> dict[str, Any]:
    """Decode prediction maps and compute detection metrics."""
    detection_class_names = get_detection_class_names(config)
    iou_threshold = float(config["detector"]["eval_iou_threshold"])
    ground_truth_batches = []
    prediction_batches = []
    for sample, prediction_map in zip(sample_records, prediction_maps):
        annotation = normalize_sidecar_annotation(load_sidecar_annotation(sample.bin_path))
        ground_truth_batches.append(annotation_to_detections(annotation, config))
        prediction_batches.append(decode_detection_map(prediction_map, config))
    return compute_detection_metrics(ground_truth_batches, prediction_batches, detection_class_names, iou_threshold)


def build_arg_parser() -> argparse.ArgumentParser:
    """Build the CLI argument parser."""
    parser = argparse.ArgumentParser(description="Validate a TFLite detector model on a processed thermal split")
    parser.add_argument("--config", type=Path, default=None, help="Path to dataset_config.json")
    parser.add_argument("--processed-root", type=Path, default=None, help="Processed dataset root, default from config")
    parser.add_argument("--model", type=Path, required=True, help="TFLite model path")
    parser.add_argument("--keras-model", type=Path, default=None, help="Optional Keras model path for consistency checks")
    parser.add_argument("--split", choices=("train", "val", "test"), default="test", help="Dataset split to validate")
    parser.add_argument("--report-dir", type=Path, default=None, help="Validation report output directory")
    return parser


def main() -> int:
    """Program entry point."""
    args = build_arg_parser().parse_args()
    tf_module = require_tensorflow()
    dataset_config = load_dataset_config(args.config)
    class_names = get_class_names(dataset_config)
    detection_class_names = get_detection_class_names(dataset_config)
    processed_root = (
        args.processed_root
        if args.processed_root is not None
        else resolve_workspace_path(dataset_config["dataset_paths"]["processed_root"])
    )
    report_dir = (
        args.report_dir
        if args.report_dir is not None
        else resolve_workspace_path(dataset_config["artifacts"]["report_dir"]) / "validation" / f"{args.model.stem}_{args.split}"
    )
    ensure_dir(report_dir)

    if not args.model.exists():
        raise SystemExit(f"TFLite model not found: {args.model}")

    sample_records = scan_processed_split(processed_root, args.split, class_names)
    if not sample_records:
        raise SystemExit(f"no samples found in split: {args.split}")

    interpreter = load_tflite_interpreter(args.model)
    evaluation = evaluate_tflite(interpreter, sample_records, dataset_config)
    detection_report_text = format_detection_report(detection_class_names, evaluation["detection_metrics"])
    suggestions = build_collection_suggestions(detection_class_names, evaluation["detection_metrics"])
    tflite_meta = inspect_tflite_model(args.model)

    (report_dir / "detection_report.txt").write_text(detection_report_text + "\n", encoding="utf-8")
    (report_dir / "collection_suggestions.txt").write_text("\n".join(suggestions) + ("\n" if suggestions else ""), encoding="utf-8")

    report_payload: dict[str, Any] = {
        "tflite_model": str(args.model),
        "tflite_io": tflite_meta,
        "split": args.split,
        "detection_metrics": evaluation["detection_metrics"],
        "mean_confidence": evaluation["mean_confidence"],
        "collection_suggestions": suggestions,
    }

    if args.keras_model is not None:
        if not args.keras_model.exists():
            raise SystemExit(f"Keras model not found: {args.keras_model}")
        keras_model = tf_module.keras.models.load_model(args.keras_model, custom_objects=get_custom_objects())
        report_payload["keras_consistency"] = compare_with_keras(keras_model, interpreter, sample_records, dataset_config)

    save_json(report_dir / "validation_summary.json", make_json_safe(report_payload))

    print("detection report:")
    print(detection_report_text)
    print("")
    print(f"mean confidence: {evaluation['mean_confidence']:.4f}")
    print(f"tflite inputs:  {tflite_meta['inputs']}")
    print(f"tflite outputs: {tflite_meta['outputs']}")
    if "keras_consistency" in report_payload:
        print(f"keras/tflite mean abs delta: {report_payload['keras_consistency']['mean_abs_delta']:.6f}")
        print(f"keras micro f1@IoU:         {report_payload['keras_consistency']['keras_detection_metrics']['micro_f1_score']:.4f}")
        print(f"tflite micro f1@IoU:        {report_payload['keras_consistency']['tflite_detection_metrics']['micro_f1_score']:.4f}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
