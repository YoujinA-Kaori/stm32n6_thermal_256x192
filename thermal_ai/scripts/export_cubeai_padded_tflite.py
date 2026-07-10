#!/usr/bin/env python3
"""Export an int8 TFLite model with a CubeAI-friendly padded detector output."""

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
from src.metrics import compute_detection_metrics, format_detection_report
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
        progressed = False
        for class_name in class_names:
            if buckets[class_name]:
                selected.append(buckets[class_name].pop())
                progressed = True
                if len(selected) >= limit:
                    break
        if not progressed:
            break
    return selected


def representative_dataset_generator(sample_records, config: dict[str, Any]):
    """Yield normalized input tensors for int8 calibration."""
    for sample in sample_records:
        frame_u16 = load_temp14_frame(sample.bin_path, config)
        frame_input = normalize_temp14_frame(frame_u16, config, add_channel_axis=True)
        yield [frame_input[np.newaxis, ...].astype(np.float32)]


def build_padded_model(tf_module, base_model, output_channels: int):
    """Rebuild the final 1x1 detector head with one or more zero-initialized dummy channels."""
    current_channels = int(base_model.output_shape[-1])
    if current_channels > output_channels:
        raise ValueError(f"model already has {current_channels} channels, cannot pad to {output_channels}")
    if current_channels == output_channels:
        return base_model

    base_head = base_model.get_layer("detection_head")
    head_input = base_head.input
    padded_head = tf_module.keras.layers.Conv2D(
        output_channels,
        kernel_size=1,
        padding="same",
        name="detection_head_cubeai_padded",
    )(head_input)
    padded_model = tf_module.keras.Model(
        inputs=base_model.input,
        outputs=padded_head,
        name=f"{base_model.name}_cubeai_padded",
    )

    base_kernel, base_bias = base_head.get_weights()
    padded_kernel = np.zeros(base_kernel.shape[:-1] + (output_channels,), dtype=base_kernel.dtype)
    padded_bias = np.zeros((output_channels,), dtype=base_bias.dtype)
    padded_kernel[..., :current_channels] = base_kernel
    padded_bias[:current_channels] = base_bias
    for channel_index in range(current_channels, output_channels):
        padded_bias[channel_index] = np.array(-12.0, dtype=base_bias.dtype)
    padded_model.get_layer("detection_head_cubeai_padded").set_weights([padded_kernel, padded_bias])
    return padded_model


def evaluate_tflite_model(tflite_path: Path, sample_records, config: dict[str, Any]) -> dict[str, Any]:
    """Run TFLite inference and compute decoded detector metrics."""
    interpreter = load_tflite_interpreter(tflite_path)
    detection_class_names = get_detection_class_names(config)
    iou_threshold = float(config["detector"]["eval_iou_threshold"])
    ground_truth_batches = []
    prediction_batches = []

    for sample in sample_records:
        frame_u16 = load_temp14_frame(sample.bin_path, config)
        frame_input = normalize_temp14_frame(frame_u16, config, add_channel_axis=True)
        prediction_map = run_tflite_inference(interpreter, frame_input)[0][0]
        annotation = normalize_sidecar_annotation(load_sidecar_annotation(sample.bin_path))
        ground_truth_batches.append(annotation_to_detections(annotation, config))
        prediction_batches.append(decode_detection_map(prediction_map, config))

    return compute_detection_metrics(ground_truth_batches, prediction_batches, detection_class_names, iou_threshold)


def build_arg_parser() -> argparse.ArgumentParser:
    """Build the CLI argument parser."""
    parser = argparse.ArgumentParser(description="Export CubeAI padded int8 TFLite detector")
    parser.add_argument("--config", type=Path, default=None, help="Path to dataset_config.json")
    parser.add_argument("--processed-root", type=Path, default=None, help="Processed dataset root")
    parser.add_argument("--model", type=Path, default=None, help="Keras model path")
    parser.add_argument("--output-dir", type=Path, default=None, help="Model artifact output directory")
    parser.add_argument("--report-dir", type=Path, default=None, help="Report output directory")
    parser.add_argument("--output-channels", type=int, default=8, help="Padded output channel count")
    parser.add_argument("--representative-count", type=int, default=128)
    parser.add_argument("--seed", type=int, default=42)
    return parser


def main() -> int:
    """Program entry point."""
    args = build_arg_parser().parse_args()
    tf_module = require_tensorflow()
    config = load_dataset_config(args.config)
    class_names = get_class_names(config)
    processed_root = (
        args.processed_root
        if args.processed_root is not None
        else resolve_workspace_path(config["dataset_paths"]["processed_root"])
    )
    output_dir = (
        args.output_dir
        if args.output_dir is not None
        else resolve_workspace_path(config["artifacts"]["model_dir"])
    )
    model_path = args.model if args.model is not None else output_dir / "best_model.keras"
    report_dir = (
        args.report_dir
        if args.report_dir is not None
        else resolve_workspace_path(config["artifacts"]["report_dir"]) / "tflite" / "cubeai_padded"
    )
    ensure_dir(output_dir)
    ensure_dir(report_dir)

    if not model_path.exists():
        raise SystemExit(f"Keras model not found: {model_path}")

    base_model = tf_module.keras.models.load_model(model_path, custom_objects=get_custom_objects())
    padded_model = build_padded_model(tf_module, base_model, args.output_channels)
    train_samples = scan_processed_split(processed_root, "train", class_names)
    test_samples = scan_processed_split(processed_root, "test", class_names)
    if not train_samples or not test_samples:
        raise SystemExit("train and test splits must exist before export")

    representative_samples = select_balanced_samples(train_samples, class_names, args.representative_count, args.seed)
    tflite_path = output_dir / "best_model_int8_cubeai_padded.tflite"
    converter = tf_module.lite.TFLiteConverter.from_keras_model(padded_model)
    converter.optimizations = [tf_module.lite.Optimize.DEFAULT]
    converter.representative_dataset = lambda: representative_dataset_generator(representative_samples, config)
    converter.target_spec.supported_ops = [tf_module.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf_module.int8
    converter.inference_output_type = tf_module.int8
    tflite_path.write_bytes(converter.convert())

    tflite_meta = inspect_tflite_model(tflite_path)
    metrics = evaluate_tflite_model(tflite_path, test_samples, config)
    report_text = format_detection_report(get_detection_class_names(config), metrics)
    (report_dir / "detection_report.txt").write_text(report_text + "\n", encoding="utf-8")
    save_json(
        report_dir / "cubeai_padded_export_report.json",
        make_json_safe(
            {
                "source_keras_model": str(model_path),
                "tflite_model": str(tflite_path),
                "tflite_io": tflite_meta,
                "metrics": metrics,
                "representative_count": len(representative_samples),
            }
        ),
    )

    print(f"cubeai padded int8 tflite: {tflite_path}")
    print(f"tflite io: {tflite_meta}")
    print("")
    print(report_text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
