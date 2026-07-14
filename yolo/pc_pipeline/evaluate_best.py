#!/usr/bin/env python3
"""Evaluate an already-trained PCB detector and create a prediction sheet."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import torch
from ultralytics import YOLO

from train_and_evaluate import make_contact_sheet, select_test_images


def parse_args() -> argparse.Namespace:
    script_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--data",
        type=Path,
        default=script_dir.parent / "PCB-dataset-common9" / "dataset.yaml",
    )
    parser.add_argument(
        "--weights",
        type=Path,
        default=(
            script_dir.parent
            / "pc_runs"
            / "common9_yolov8n_320_w0"
            / "weights"
            / "best.pt"
        ),
    )
    parser.add_argument("--imgsz", type=int, default=320)
    parser.add_argument("--batch", type=int, default=16)
    parser.add_argument("--workers", type=int, default=0)
    parser.add_argument("--device", default="0")
    parser.add_argument("--name", default="common9_yolov8n_320_w0_test")
    parser.add_argument("--samples", type=int, default=12)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    data_yaml = args.data.resolve()
    weights = args.weights.resolve()
    run_root = (Path(__file__).resolve().parent.parent / "pc_runs").resolve()
    test_dir = run_root / args.name

    if not data_yaml.is_file():
        raise FileNotFoundError(f"Dataset YAML not found: {data_yaml}")
    if not weights.is_file():
        raise FileNotFoundError(f"Weights not found: {weights}")
    if not torch.cuda.is_available():
        raise RuntimeError("CUDA is unavailable; refusing accidental CPU evaluation")

    model = YOLO(str(weights))
    metrics = model.val(
        data=str(data_yaml),
        split="test",
        imgsz=args.imgsz,
        batch=args.batch,
        workers=args.workers,
        device=args.device,
        project=str(run_root),
        name=args.name,
        exist_ok=True,
        plots=True,
        verbose=True,
    )

    class_names = model.names
    per_class = {}
    for class_id in range(len(class_names)):
        per_class[class_names[class_id]] = {
            "precision": float(metrics.box.p[class_id]),
            "recall": float(metrics.box.r[class_id]),
            "map50": float(metrics.box.ap50[class_id]),
            "map50_95": float(metrics.box.maps[class_id]),
        }

    sample_images = select_test_images(data_yaml, args.samples)
    predictions = model.predict(
        source=[str(path) for path in sample_images],
        imgsz=args.imgsz,
        conf=0.35,
        iou=0.55,
        max_det=100,
        device=args.device,
        verbose=False,
    )
    contact_sheet = test_dir / "prediction_contact_sheet.jpg"
    make_contact_sheet(predictions, contact_sheet)

    summary = {
        "weights": str(weights),
        "dataset": str(data_yaml),
        "split": "test",
        "imgsz": args.imgsz,
        "precision": float(metrics.box.mp),
        "recall": float(metrics.box.mr),
        "map50": float(metrics.box.map50),
        "map75": float(metrics.box.map75),
        "map50_95": float(metrics.box.map),
        "per_class": per_class,
        "prediction_contact_sheet": str(contact_sheet.resolve()),
        "prediction_samples": [str(path.resolve()) for path in sample_images],
    }
    summary_path = test_dir / "pc_evaluation_summary.json"
    summary_path.write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    print(f"PC evaluation complete: {summary_path}")


if __name__ == "__main__":
    main()
