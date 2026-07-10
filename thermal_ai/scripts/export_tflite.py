#!/usr/bin/env python3
"""Export float32 and int8 TFLite detector models and compare them with Keras."""

from __future__ import annotations

import argparse
import random
from collections import defaultdict
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
from src.metrics import compute_detection_metrics
from src.model import get_custom_objects, require_tensorflow
from src.tflite_utils import inspect_tflite_model, load_tflite_interpreter, run_tflite_inference


def select_balanced_samples(sample_records, class_names: list[str], limit: int, seed: int):
    """Select a class-balanced subset using the primary class folder as the balancing key."""
    rng = random.Random(seed)
    buckets: dict[str, list[Any]] = defaultdict(list)
    for sample in sample_records:
        buckets[sample.class_name].append(sample)

    for class_name in class_names:
        rng.shuffle(buckets[class_name])

    selected = []
    while len(selected) < limit:
        progress = False
        for class_name in class_names:
            if buckets[class_name]:
                selected.append(buckets[class_name].pop())
                progress = True
                if len(selected) >= limit:
                    break
        if not progress:
            break

    return selected


def representative_dataset_generator(sample_records, config: dict[str, Any]):
    """Yield normalized sample tensors for int8 representative calibration."""
    for sample in sample_records:
        frame_u16 = load_temp14_frame(sample.bin_path, config)
        frame_input = normalize_temp14_frame(frame_u16, config, add_channel_axis=True)
        yield [frame_input[np.newaxis, ...].astype(np.float32)]


def evaluate_detection_batches(prediction_maps: list[np.ndarray], sample_records, config: dict[str, Any]) -> dict[str, Any]:
    """Decode detector outputs and compute dataset-level metrics."""
    detection_class_names = get_detection_class_names(config)
    iou_threshold = float(config["detector"]["eval_iou_threshold"])
    ground_truth_batches = []
    prediction_batches = []

    for sample, prediction_map in zip(sample_records, prediction_maps):
        annotation = normalize_sidecar_annotation(load_sidecar_annotation(sample.bin_path))
        ground_truth_batches.append(annotation_to_detections(annotation, config))
        prediction_batches.append(decode_detection_map(prediction_map, config))

    return compute_detection_metrics(ground_truth_batches, prediction_batches, detection_class_names, iou_threshold)


def compare_keras_and_tflite(model, tflite_path: Path, sample_records, config: dict[str, Any]) -> dict[str, Any]:
    """Compare detector outputs and decoded metrics between Keras and TFLite."""
    interpreter = load_tflite_interpreter(tflite_path)
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
        "keras_detection_metrics": evaluate_detection_batches(keras_prediction_maps, sample_records, config),
        "tflite_detection_metrics": evaluate_detection_batches(tflite_prediction_maps, sample_records, config),
    }


def build_arg_parser() -> argparse.ArgumentParser:
    """Build the CLI argument parser."""
    parser = argparse.ArgumentParser(description="Export float32 and int8 TFLite detector models")
    parser.add_argument("--config", type=Path, default=None, help="Path to dataset_config.json")
    parser.add_argument("--processed-root", type=Path, default=None, help="Processed dataset root, default from config")
    parser.add_argument("--model", type=Path, default=None, help="Keras model path, default best_model.keras")
    parser.add_argument("--output-dir", type=Path, default=None, help="TFLite artifact output directory")
    parser.add_argument("--report-dir", type=Path, default=None, help="TFLite report output directory")
    parser.add_argument("--representative-count", type=int, default=128, help="Representative dataset sample count")
    parser.add_argument("--compare-count", type=int, default=64, help="Comparison sample count")
    parser.add_argument("--seed", type=int, default=42, help="Sampling seed")
    return parser


def main() -> int:
    """Program entry point."""
    args = build_arg_parser().parse_args()
    tf_module = require_tensorflow()
    dataset_config = load_dataset_config(args.config)
    class_names = get_class_names(dataset_config)

    processed_root = (
        args.processed_root
        if args.processed_root is not None
        else resolve_workspace_path(dataset_config["dataset_paths"]["processed_root"])
    )
    output_dir = (
        args.output_dir
        if args.output_dir is not None
        else resolve_workspace_path(dataset_config["artifacts"]["model_dir"])
    )
    model_path = args.model if args.model is not None else output_dir / "best_model.keras"
    report_dir = (
        args.report_dir
        if args.report_dir is not None
        else resolve_workspace_path(dataset_config["artifacts"]["report_dir"]) / "tflite" / model_path.stem
    )
    ensure_dir(output_dir)
    ensure_dir(report_dir)

    if not model_path.exists():
        raise SystemExit(f"keras model not found: {model_path}")

    model = tf_module.keras.models.load_model(model_path, custom_objects=get_custom_objects())
    train_samples = scan_processed_split(processed_root, "train", class_names)
    test_samples = scan_processed_split(processed_root, "test", class_names)
    if not train_samples or not test_samples:
        raise SystemExit("train and test splits must exist before TFLite export")

    representative_samples = select_balanced_samples(train_samples, class_names, args.representative_count, args.seed)
    comparison_samples = select_balanced_samples(test_samples, class_names, args.compare_count, args.seed)

    float_tflite_path = output_dir / f"{model_path.stem}_float32.tflite"
    float_converter = tf_module.lite.TFLiteConverter.from_keras_model(model)
    float_tflite_model = float_converter.convert()
    float_tflite_path.write_bytes(float_tflite_model)

    int8_tflite_path = output_dir / f"{model_path.stem}_int8.tflite"
    int8_converter = tf_module.lite.TFLiteConverter.from_keras_model(model)
    int8_converter.optimizations = [tf_module.lite.Optimize.DEFAULT]
    int8_converter.representative_dataset = lambda: representative_dataset_generator(representative_samples, dataset_config)
    int8_converter.target_spec.supported_ops = [tf_module.lite.OpsSet.TFLITE_BUILTINS_INT8]
    int8_converter.inference_input_type = tf_module.int8
    int8_converter.inference_output_type = tf_module.int8
    int8_tflite_model = int8_converter.convert()
    int8_tflite_path.write_bytes(int8_tflite_model)

    float_meta = inspect_tflite_model(float_tflite_path)
    int8_meta = inspect_tflite_model(int8_tflite_path)
    float_compare = compare_keras_and_tflite(model, float_tflite_path, comparison_samples, dataset_config)
    int8_compare = compare_keras_and_tflite(model, int8_tflite_path, comparison_samples, dataset_config)

    report_payload = {
        "keras_model": str(model_path),
        "float32_tflite": float_meta,
        "int8_tflite": int8_meta,
        "float32_comparison": float_compare,
        "int8_comparison": int8_compare,
        "representative_count": len(representative_samples),
        "comparison_count": len(comparison_samples),
    }
    save_json(report_dir / "tflite_export_report.json", make_json_safe(report_payload))

    print(f"float32 tflite: {float_tflite_path}")
    print(f"int8 tflite:    {int8_tflite_path}")
    print("")
    print("float32 inputs/outputs:")
    print(float_meta)
    print("")
    print("int8 inputs/outputs:")
    print(int8_meta)
    print("")
    print(f"float32 mean abs delta:      {float_compare['mean_abs_delta']:.6f}")
    print(f"float32 micro f1@IoU:        {float_compare['tflite_detection_metrics']['micro_f1_score']:.4f}")
    print(f"int8 mean abs delta:         {int8_compare['mean_abs_delta']:.6f}")
    print(f"int8 micro f1@IoU:           {int8_compare['tflite_detection_metrics']['micro_f1_score']:.4f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
