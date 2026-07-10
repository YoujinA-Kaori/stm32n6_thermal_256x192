#!/usr/bin/env python3
"""Train the lightweight thermal object detector."""

from __future__ import annotations

import argparse
import random
from pathlib import Path
from typing import Any

import numpy as np

import _bootstrap

_bootstrap.setup_python_path()

from src.common import (
    annotation_to_detections,
    build_detection_target,
    count_detection_target_collisions,
    ensure_dir,
    get_class_names,
    get_detection_class_names,
    load_dataset_config,
    load_sidecar_annotation,
    load_temp14_frame,
    load_train_config,
    make_json_safe,
    normalize_sidecar_annotation,
    normalize_temp14_frame,
    resolve_workspace_path,
    save_json,
    scan_processed_split,
)
from src.metrics import build_collection_suggestions, compute_detection_metrics, format_detection_report
from src.model import build_cnn_model, get_custom_objects, require_tensorflow


def set_global_seed(seed: int) -> None:
    """Seed Python, NumPy, and TensorFlow RNGs for reproducibility."""
    tf_module = require_tensorflow()
    random.seed(seed)
    np.random.seed(seed)
    tf_module.random.set_seed(seed)


def validate_sample_annotation(sample, config: dict[str, Any]) -> None:
    """Ensure that one sample annotation is compatible with the detection workflow."""
    annotation = normalize_sidecar_annotation(load_sidecar_annotation(sample.bin_path))
    frame_height = int(config["input"]["height"])
    frame_width = int(config["input"]["width"])
    dataset_class_names = set(get_class_names(config))
    detection_class_names = set(get_detection_class_names(config))

    if annotation is None or annotation.is_empty or not annotation.objects:
        raise SystemExit(
            f"missing bbox annotation for detector sample: {sample.bin_path} "
            f"(expected a JSON sidecar with an objects list)"
        )

    annotated_class_names = {annotated_object.class_name for annotated_object in annotation.objects}
    if sample.class_name in detection_class_names and sample.class_name not in annotated_class_names:
        raise SystemExit(
            f"primary folder class is not present in annotation objects: {sample.bin_path} "
            f"(folder={sample.class_name}, objects={sorted(annotated_class_names)})"
        )

    if (
        sample.class_name in detection_class_names
        and annotation.primary_class_name is not None
        and annotation.primary_class_name != sample.class_name
    ):
        raise SystemExit(
            f"primary_class_name mismatch for {sample.bin_path}: "
            f"folder={sample.class_name}, annotation={annotation.primary_class_name}"
        )

    unknown_classes = sorted(annotated_class_names - dataset_class_names)
    if unknown_classes:
        raise SystemExit(f"unknown annotation classes in {sample.bin_path}: {unknown_classes}")

    for annotated_object in annotation.objects:
        if not (0.0 <= annotated_object.x_min < annotated_object.x_max <= float(frame_width)):
            raise SystemExit(f"invalid bbox x-range in {sample.bin_path}: {annotated_object}")
        if not (0.0 <= annotated_object.y_min < annotated_object.y_max <= float(frame_height)):
            raise SystemExit(f"invalid bbox y-range in {sample.bin_path}: {annotated_object}")


def build_tf_dataset(
    sample_records,
    config: dict[str, Any],
    batch_size: int,
    shuffle: bool,
    seed: int,
):
    """Build a tf.data pipeline for anchor-free detection targets."""
    tf_module = require_tensorflow()
    records = list(sample_records)
    if not records:
        raise ValueError("dataset split is empty")

    if shuffle:
        rng = random.Random(seed)
        rng.shuffle(records)

    height = int(config["input"]["height"])
    width = int(config["input"]["width"])
    channels = int(config["input"]["channels"])
    grid_height = int(config["detector"]["grid_height"])
    grid_width = int(config["detector"]["grid_width"])
    output_channels = 1 + 4 + len(get_detection_class_names(config))

    for sample in records:
        validate_sample_annotation(sample, config)

    def generator():
        """Yield normalized input tensors plus detector supervision."""
        for sample in records:
            frame_u16 = load_temp14_frame(sample.bin_path, config)
            frame_input = normalize_temp14_frame(frame_u16, config, add_channel_axis=True).astype(np.float32)
            annotation = normalize_sidecar_annotation(load_sidecar_annotation(sample.bin_path))
            target = build_detection_target(annotation, config).astype(np.float32)
            yield frame_input, target

    dataset = tf_module.data.Dataset.from_generator(
        generator,
        output_signature=(
            tf_module.TensorSpec(shape=(height, width, channels), dtype=tf_module.float32),
            tf_module.TensorSpec(shape=(grid_height, grid_width, output_channels), dtype=tf_module.float32),
        ),
    )
    if shuffle:
        dataset = dataset.shuffle(buffer_size=len(records), seed=seed, reshuffle_each_iteration=True)
    dataset = dataset.batch(batch_size)
    dataset = dataset.prefetch(tf_module.data.AUTOTUNE)
    return dataset


def evaluate_split(model, dataset, sample_records, split_name: str, config: dict[str, Any]) -> dict[str, Any]:
    """Evaluate one dataset split and collect detection metrics."""
    evaluation = model.evaluate(dataset, verbose=0, return_dict=True)
    prediction_maps = model.predict(dataset, verbose=0)
    detection_class_names = get_detection_class_names(config)
    iou_threshold = float(config["detector"]["eval_iou_threshold"])

    ground_truth_batches = []
    prediction_batches = []
    collision_count = 0

    for sample, prediction_map in zip(sample_records, prediction_maps):
        annotation = normalize_sidecar_annotation(load_sidecar_annotation(sample.bin_path))
        ground_truth_batches.append(annotation_to_detections(annotation, config))
        prediction_batches.append(_bootstrap_import_decode_detection_map(prediction_map, config))
        collision_count += count_detection_target_collisions(annotation, config)

    detection_metrics = compute_detection_metrics(
        ground_truth_batches,
        prediction_batches,
        detection_class_names,
        iou_threshold,
    )
    return {
        "split": split_name,
        "loss": float(evaluation.get("loss", 0.0)),
        "objectness_accuracy": float(evaluation.get("detection_objectness_accuracy", 0.0)),
        "bbox_mae": float(evaluation.get("detection_bbox_mae", 0.0)),
        "class_accuracy": float(evaluation.get("detection_class_accuracy", 0.0)),
        "detection_metrics": detection_metrics,
        "assignment_collisions": int(collision_count),
    }


def _bootstrap_import_decode_detection_map(prediction_map: np.ndarray, config: dict[str, Any]):
    """Delay import to keep top-level import list compact."""
    from src.common import decode_detection_map

    return decode_detection_map(prediction_map, config)


def build_arg_parser() -> argparse.ArgumentParser:
    """Build the CLI argument parser."""
    parser = argparse.ArgumentParser(description="Train the STM32N6 thermal object detector")
    parser.add_argument("--config", type=Path, default=None, help="Path to dataset_config.json")
    parser.add_argument("--train-config", type=Path, default=None, help="Path to train_config.json")
    parser.add_argument("--processed-root", type=Path, default=None, help="Processed dataset root, default from config")
    parser.add_argument("--output-dir", type=Path, default=None, help="Model artifact output directory")
    parser.add_argument("--report-dir", type=Path, default=None, help="Training report output directory")
    return parser


def main() -> int:
    """Program entry point."""
    args = build_arg_parser().parse_args()
    tf_module = require_tensorflow()
    dataset_config = load_dataset_config(args.config)
    train_config = load_train_config(args.train_config)
    class_names = get_class_names(dataset_config)
    detection_class_names = get_detection_class_names(dataset_config)

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
    report_dir = (
        args.report_dir
        if args.report_dir is not None
        else resolve_workspace_path(dataset_config["artifacts"]["report_dir"]) / "training"
    )
    ensure_dir(output_dir)
    ensure_dir(report_dir)

    seed = int(train_config["seed"])
    batch_size = int(train_config["batch_size"])
    epochs = int(train_config["epochs"])
    set_global_seed(seed)

    train_samples = scan_processed_split(processed_root, "train", class_names)
    val_samples = scan_processed_split(processed_root, "val", class_names)
    test_samples = scan_processed_split(processed_root, "test", class_names)
    if not train_samples or not val_samples or not test_samples:
        raise SystemExit("train/val/test splits must all contain samples before training")

    train_dataset = build_tf_dataset(train_samples, dataset_config, batch_size, shuffle=True, seed=seed)
    train_eval_dataset = build_tf_dataset(train_samples, dataset_config, batch_size, shuffle=False, seed=seed)
    val_dataset = build_tf_dataset(val_samples, dataset_config, batch_size, shuffle=False, seed=seed)
    test_dataset = build_tf_dataset(test_samples, dataset_config, batch_size, shuffle=False, seed=seed)

    model = build_cnn_model(dataset_config, train_config)
    best_model_path = output_dir / "best_model.keras"
    final_model_path = output_dir / "final_model.keras"

    callbacks = [
        tf_module.keras.callbacks.ModelCheckpoint(
            filepath=str(best_model_path),
            monitor="val_loss",
            mode="min",
            save_best_only=True,
        ),
        tf_module.keras.callbacks.EarlyStopping(
            monitor="val_loss",
            mode="min",
            patience=int(train_config["early_stopping_patience"]),
            restore_best_weights=False,
        ),
        tf_module.keras.callbacks.ReduceLROnPlateau(
            monitor="val_loss",
            factor=float(train_config["reduce_lr_factor"]),
            patience=int(train_config["reduce_lr_patience"]),
            min_lr=float(train_config["min_learning_rate"]),
            verbose=1,
        ),
    ]

    history = model.fit(
        train_dataset,
        validation_data=val_dataset,
        epochs=epochs,
        callbacks=callbacks,
        verbose=1,
    )

    model.save(final_model_path)
    best_model = tf_module.keras.models.load_model(best_model_path, custom_objects=get_custom_objects())

    train_result = evaluate_split(best_model, train_eval_dataset, train_samples, "train", dataset_config)
    val_result = evaluate_split(best_model, val_dataset, val_samples, "val", dataset_config)
    test_result = evaluate_split(best_model, test_dataset, test_samples, "test", dataset_config)

    save_json(output_dir / "class_names.json", class_names)
    save_json(output_dir / "detection_class_names.json", detection_class_names)

    detection_report_text = format_detection_report(detection_class_names, test_result["detection_metrics"])
    suggestions = build_collection_suggestions(detection_class_names, test_result["detection_metrics"])

    (report_dir / "detection_report.txt").write_text(detection_report_text + "\n", encoding="utf-8")
    (report_dir / "collection_suggestions.txt").write_text("\n".join(suggestions) + ("\n" if suggestions else ""), encoding="utf-8")

    summary_payload = {
        "history": history.history,
        "train_loss": train_result["loss"],
        "val_loss": val_result["loss"],
        "test_loss": test_result["loss"],
        "train_objectness_accuracy": train_result["objectness_accuracy"],
        "val_objectness_accuracy": val_result["objectness_accuracy"],
        "test_objectness_accuracy": test_result["objectness_accuracy"],
        "train_bbox_mae": train_result["bbox_mae"],
        "val_bbox_mae": val_result["bbox_mae"],
        "test_bbox_mae": test_result["bbox_mae"],
        "train_class_accuracy": train_result["class_accuracy"],
        "val_class_accuracy": val_result["class_accuracy"],
        "test_class_accuracy": test_result["class_accuracy"],
        "train_detection_metrics": train_result["detection_metrics"],
        "val_detection_metrics": val_result["detection_metrics"],
        "test_detection_metrics": test_result["detection_metrics"],
        "train_assignment_collisions": train_result["assignment_collisions"],
        "val_assignment_collisions": val_result["assignment_collisions"],
        "test_assignment_collisions": test_result["assignment_collisions"],
        "collection_suggestions": suggestions,
        "class_names": class_names,
        "detection_class_names": detection_class_names,
        "best_model_path": str(best_model_path),
        "final_model_path": str(final_model_path),
    }
    save_json(report_dir / "training_summary.json", make_json_safe(summary_payload))

    print(f"train loss:                  {train_result['loss']:.4f}")
    print(f"val loss:                    {val_result['loss']:.4f}")
    print(f"test loss:                   {test_result['loss']:.4f}")
    print(f"test objectness accuracy:    {test_result['objectness_accuracy']:.4f}")
    print(f"test bbox mae:               {test_result['bbox_mae']:.4f}")
    print(f"test class accuracy:         {test_result['class_accuracy']:.4f}")
    print(f"test micro precision@IoU:    {test_result['detection_metrics']['micro_precision']:.4f}")
    print(f"test micro recall@IoU:       {test_result['detection_metrics']['micro_recall']:.4f}")
    print(f"test micro f1@IoU:           {test_result['detection_metrics']['micro_f1_score']:.4f}")
    print(f"test mean IoU:               {test_result['detection_metrics']['mean_iou']:.4f}")
    if test_result["detection_metrics"]["mean_center_error_px"] is not None:
        print(f"test mean center error px:   {test_result['detection_metrics']['mean_center_error_px']:.2f}")
    print("")
    print("detection report:")
    print(detection_report_text)
    if suggestions:
        print("")
        print("data collection suggestions:")
        for line in suggestions:
            print(f"- {line}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
