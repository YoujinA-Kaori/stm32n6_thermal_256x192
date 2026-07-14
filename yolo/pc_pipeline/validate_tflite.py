#!/usr/bin/env python3
"""Validate the exported full-INT8 TFLite detector on the held-out test set."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from ultralytics import YOLO


def parse_args() -> argparse.Namespace:
    script_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--data",
        type=Path,
        default=script_dir.parent / "PCB-dataset-common9" / "dataset.yaml",
    )
    parser.add_argument(
        "--model",
        type=Path,
        default=(
            script_dir.parent
            / "pc_runs"
            / "common9_yolov8n_320_w0"
            / "weights"
            / "best_saved_model"
            / "best_full_integer_quant.tflite"
        ),
    )
    parser.add_argument("--imgsz", type=int, default=320)
    parser.add_argument("--name", default="common9_yolov8n_320_w0_tflite_int8_test")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    data_yaml = args.data.resolve()
    model_path = args.model.resolve()
    run_root = (Path(__file__).resolve().parent.parent / "pc_runs").resolve()

    if not data_yaml.is_file():
        raise FileNotFoundError(f"Dataset YAML not found: {data_yaml}")
    if not model_path.is_file():
        raise FileNotFoundError(f"TFLite model not found: {model_path}")

    model = YOLO(str(model_path), task="detect")
    metrics = model.val(
        data=str(data_yaml),
        split="test",
        imgsz=args.imgsz,
        batch=1,
        workers=0,
        device="cpu",
        project=str(run_root),
        name=args.name,
        exist_ok=True,
        plots=True,
        verbose=True,
    )

    class_names = model.names
    summary = {
        "model": str(model_path),
        "dataset": str(data_yaml),
        "split": "test",
        "imgsz": args.imgsz,
        "precision": float(metrics.box.mp),
        "recall": float(metrics.box.mr),
        "map50": float(metrics.box.map50),
        "map75": float(metrics.box.map75),
        "map50_95": float(metrics.box.map),
        "per_class": {
            class_names[class_id]: {
                "precision": float(metrics.box.p[class_id]),
                "recall": float(metrics.box.r[class_id]),
                "map50": float(metrics.box.ap50[class_id]),
                "map50_95": float(metrics.box.maps[class_id]),
            }
            for class_id in range(len(class_names))
        },
    }
    summary_path = run_root / args.name / "tflite_evaluation_summary.json"
    summary_path.write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    print(f"TFLite evaluation complete: {summary_path}")


if __name__ == "__main__":
    main()
