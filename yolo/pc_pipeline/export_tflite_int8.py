#!/usr/bin/env python3
"""Export the trained nine-class YOLOv8n model to calibrated INT8 TFLite."""

from __future__ import annotations

import argparse
from pathlib import Path

from ultralytics import YOLO


def parse_args() -> argparse.Namespace:
    script_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
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
    parser.add_argument(
        "--data",
        type=Path,
        default=script_dir / "calibration_dataset.yaml",
    )
    parser.add_argument("--imgsz", type=int, default=320)
    parser.add_argument("--batch", type=int, default=1)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    weights = args.weights.resolve()
    data_yaml = args.data.resolve()
    if not weights.is_file():
        raise FileNotFoundError(f"Weights not found: {weights}")
    if not data_yaml.is_file():
        raise FileNotFoundError(f"Calibration YAML not found: {data_yaml}")

    model = YOLO(str(weights))
    exported = model.export(
        format="tflite",
        imgsz=args.imgsz,
        int8=True,
        data=str(data_yaml),
        batch=args.batch,
        nms=False,
        device="cpu",
    )
    print(f"TFLite export complete: {exported}")


if __name__ == "__main__":
    main()
