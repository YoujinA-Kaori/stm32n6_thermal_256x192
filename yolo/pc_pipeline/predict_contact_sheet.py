#!/usr/bin/env python3
"""Create a repeatable qualitative prediction sheet for a YOLO model."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

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
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--imgsz", type=int, default=320)
    parser.add_argument("--samples", type=int, default=12)
    parser.add_argument("--conf", type=float, default=0.35)
    parser.add_argument("--iou", type=float, default=0.55)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    data_yaml = args.data.resolve()
    model_path = args.model.resolve()
    run_dir = (Path(__file__).resolve().parent.parent / "pc_runs" / args.name).resolve()

    if not data_yaml.is_file():
        raise FileNotFoundError(f"Dataset YAML not found: {data_yaml}")
    if not model_path.is_file():
        raise FileNotFoundError(f"Model not found: {model_path}")

    sample_images = select_test_images(data_yaml, args.samples)
    model = YOLO(str(model_path), task="detect")
    device = "cpu" if model_path.suffix.lower() == ".tflite" else "0"
    results = []
    for image_path in sample_images:
        prediction = model.predict(
            source=str(image_path),
            imgsz=args.imgsz,
            conf=args.conf,
            iou=args.iou,
            max_det=100,
            device=device,
            verbose=False,
        )
        results.append(prediction[0])

    contact_sheet = run_dir / "prediction_contact_sheet.jpg"
    make_contact_sheet(results, contact_sheet)
    summary = {
        "model": str(model_path),
        "imgsz": args.imgsz,
        "confidence": args.conf,
        "iou": args.iou,
        "prediction_contact_sheet": str(contact_sheet),
        "samples": [str(path.resolve()) for path in sample_images],
        "detection_counts": [len(result.boxes) for result in results],
    }
    run_dir.mkdir(parents=True, exist_ok=True)
    (run_dir / "prediction_summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
