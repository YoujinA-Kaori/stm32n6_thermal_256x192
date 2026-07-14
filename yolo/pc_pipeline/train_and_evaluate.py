#!/usr/bin/env python3
"""Fine-tune the reduced PCB detector and close the PC evaluation loop."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

import cv2
import torch
from PIL import Image, ImageDraw
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
        "--weights",
        type=Path,
        default=script_dir.parent / "yolov8训练结果" / "weights" / "best.pt",
        help="Existing 22-class PCB checkpoint used as transfer-learning source",
    )
    parser.add_argument("--imgsz", type=int, default=320)
    parser.add_argument("--epochs", type=int, default=80)
    parser.add_argument("--batch", type=int, default=16)
    parser.add_argument(
        "--workers",
        type=int,
        default=0,
        help="Keep 0 on this Windows host to avoid CUDA DLL page-file failures",
    )
    parser.add_argument("--device", default="0")
    parser.add_argument("--name", default="common9_yolov8n_320")
    parser.add_argument("--samples", type=int, default=12)
    parser.add_argument(
        "--allow-cpu",
        action="store_true",
        help="Allow slow CPU training when CUDA is unavailable",
    )
    return parser.parse_args()


def select_test_images(data_yaml: Path, sample_count: int) -> list[Path]:
    dataset_root = data_yaml.parent
    image_dir = dataset_root / "test" / "images"
    label_dir = dataset_root / "test" / "labels"
    candidates: list[tuple[int, Path]] = []
    for image_path in sorted(image_dir.iterdir()):
        if not image_path.is_file():
            continue
        label_path = label_dir / f"{image_path.stem}.txt"
        if not label_path.exists():
            continue
        box_count = sum(
            1 for line in label_path.read_text(encoding="utf-8").splitlines()
            if line.strip()
        )
        candidates.append((box_count, image_path))

    if len(candidates) <= sample_count:
        return [path for _, path in candidates]

    # Density quantiles give a less cherry-picked view than taking only the
    # easiest or most crowded PCB images.
    candidates.sort(key=lambda item: (item[0], item[1].name))
    indices = {
        round(index * (len(candidates) - 1) / (sample_count - 1))
        for index in range(sample_count)
    }
    return [candidates[index][1] for index in sorted(indices)]


def make_contact_sheet(results: list, destination: Path, columns: int = 3) -> None:
    if not results:
        return

    tile_width = 480
    tile_height = 390
    rows = math.ceil(len(results) / columns)
    sheet = Image.new("RGB", (columns * tile_width, rows * tile_height), "white")
    draw = ImageDraw.Draw(sheet)

    for index, result in enumerate(results):
        source_bgr = cv2.imread(str(result.path), cv2.IMREAD_COLOR)
        if source_bgr is None:
            raise FileNotFoundError(f"Unable to read prediction image: {result.path}")
        plotted_bgr = result.plot(img=source_bgr)
        plotted_rgb = plotted_bgr[:, :, ::-1]
        image = Image.fromarray(plotted_rgb)
        image.thumbnail((tile_width, tile_height - 30), Image.Resampling.LANCZOS)
        x = (index % columns) * tile_width
        y = (index // columns) * tile_height
        paste_x = x + (tile_width - image.width) // 2
        paste_y = y + 25 + (tile_height - 30 - image.height) // 2
        sheet.paste(image, (paste_x, paste_y))
        label = f"{Path(result.path).name} | detections={len(result.boxes)}"
        draw.text((x + 8, y + 6), label, fill="black")

    destination.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(destination, quality=92)


def main() -> None:
    args = parse_args()
    data_yaml = args.data.resolve()
    weights = args.weights.resolve()
    run_root = (Path(__file__).resolve().parent.parent / "pc_runs").resolve()

    if not data_yaml.is_file():
        raise FileNotFoundError(f"Dataset YAML not found: {data_yaml}")
    if not weights.is_file():
        raise FileNotFoundError(f"Initial weights not found: {weights}")
    if not torch.cuda.is_available() and not args.allow_cpu:
        raise RuntimeError(
            "CUDA is unavailable. Refusing accidental CPU training; fix the "
            "PyTorch environment or pass --allow-cpu explicitly."
        )

    print(f"torch={torch.__version__}")
    print(f"cuda_available={torch.cuda.is_available()}")
    if torch.cuda.is_available():
        print(f"gpu={torch.cuda.get_device_name(0)}")

    model = YOLO(str(weights))
    train_result = model.train(
        data=str(data_yaml),
        imgsz=args.imgsz,
        epochs=args.epochs,
        batch=args.batch,
        workers=args.workers,
        device=args.device,
        project=str(run_root),
        name=args.name,
        exist_ok=False,
        seed=0,
        deterministic=True,
        patience=25,
        close_mosaic=10,
        cos_lr=True,
        cache=False,
        amp=True,
        plots=True,
        save=True,
        save_period=-1,
        verbose=True,
    )

    # Ultralytics 8.3.157 returns DetMetrics from train(), which has no
    # save_dir attribute. The trainer remains the authoritative run path.
    train_dir = Path(model.trainer.save_dir).resolve()
    best_weights = train_dir / "weights" / "best.pt"
    best_model = YOLO(str(best_weights))
    test_name = f"{args.name}_test"
    metrics = best_model.val(
        data=str(data_yaml),
        split="test",
        imgsz=args.imgsz,
        batch=args.batch,
        workers=args.workers,
        device=args.device,
        project=str(run_root),
        name=test_name,
        exist_ok=True,
        plots=True,
        verbose=True,
    )

    test_dir = (run_root / test_name).resolve()
    class_names = best_model.names
    per_class_map5095 = {
        class_names[class_id]: float(metrics.box.maps[class_id])
        for class_id in range(len(class_names))
    }
    summary = {
        "initial_weights": str(weights),
        "best_weights": str(best_weights),
        "dataset": str(data_yaml),
        "imgsz": args.imgsz,
        "epochs_requested": args.epochs,
        "train_directory": str(train_dir),
        "test_directory": str(test_dir),
        "precision": float(metrics.box.mp),
        "recall": float(metrics.box.mr),
        "map50": float(metrics.box.map50),
        "map75": float(metrics.box.map75),
        "map50_95": float(metrics.box.map),
        "per_class_map50_95": per_class_map5095,
    }

    sample_images = select_test_images(data_yaml, args.samples)
    predictions = best_model.predict(
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
    summary["prediction_contact_sheet"] = str(contact_sheet)
    summary["prediction_samples"] = [str(path) for path in sample_images]

    summary_path = test_dir / "pc_evaluation_summary.json"
    summary_path.write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    print(f"PC evaluation complete: {summary_path}")


if __name__ == "__main__":
    main()
