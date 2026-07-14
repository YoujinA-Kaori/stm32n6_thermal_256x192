#!/usr/bin/env python3
"""Build a reduced nine-class YOLO dataset without duplicating image data."""

from __future__ import annotations

import argparse
import json
import os
import shutil
from collections import Counter
from pathlib import Path


RETAINED_CLASSES = [
    (3, "capacitor"),
    (17, "resistor"),
    (10, "ic"),
    (5, "connector"),
    (6, "diode"),
    (21, "transistor"),
    (12, "led"),
    (11, "inductor"),
    (18, "switch"),
]
IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}


def parse_args() -> argparse.Namespace:
    script_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source",
        type=Path,
        default=script_dir.parent / "PCB-dataset",
        help="Original 22-class YOLO dataset",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=script_dir.parent / "PCB-dataset-common9",
        help="Output dataset directory",
    )
    return parser.parse_args()


def hardlink_or_copy(source: Path, destination: Path) -> str:
    if destination.exists():
        if destination.stat().st_size != source.stat().st_size:
            raise RuntimeError(f"Existing image differs from source: {destination}")
        return "existing"

    try:
        os.link(source, destination)
        return "hardlink"
    except OSError:
        shutil.copy2(source, destination)
        return "copy"


def convert_label(
    source_label: Path,
    destination_label: Path,
    class_remap: dict[int, int],
) -> tuple[Counter[int], int]:
    class_counts: Counter[int] = Counter()
    retained_lines: list[str] = []
    malformed = 0

    for line_number, raw_line in enumerate(
        source_label.read_text(encoding="utf-8").splitlines(), start=1
    ):
        line = raw_line.strip()
        if not line:
            continue

        fields = line.split()
        if len(fields) != 5:
            malformed += 1
            print(f"WARN: {source_label}:{line_number}: expected 5 fields")
            continue

        try:
            old_class = int(fields[0])
            coordinates = [float(value) for value in fields[1:]]
        except ValueError:
            malformed += 1
            print(f"WARN: {source_label}:{line_number}: invalid numeric field")
            continue

        if not all(0.0 <= value <= 1.0 for value in coordinates):
            malformed += 1
            print(f"WARN: {source_label}:{line_number}: coordinate outside [0, 1]")
            continue

        if old_class not in class_remap:
            continue

        new_class = class_remap[old_class]
        retained_lines.append(f"{new_class} {' '.join(fields[1:])}")
        class_counts[new_class] += 1

    text = "\n".join(retained_lines)
    if text:
        text += "\n"
    destination_label.write_text(text, encoding="utf-8")
    return class_counts, malformed


def write_dataset_yaml(output_root: Path) -> None:
    names = "\n".join(
        f"  {new_id}: {name}"
        for new_id, (_, name) in enumerate(RETAINED_CLASSES)
    )
    yaml_text = (
        f'path: "{output_root.resolve().as_posix()}"\n'
        "train: train/images\n"
        "val: valid/images\n"
        "test: test/images\n\n"
        f"names:\n{names}\n"
    )
    (output_root / "dataset.yaml").write_text(yaml_text, encoding="utf-8")


def main() -> None:
    args = parse_args()
    source_root = args.source.resolve()
    output_root = args.output.resolve()
    class_remap = {
        old_id: new_id
        for new_id, (old_id, _) in enumerate(RETAINED_CLASSES)
    }

    if not source_root.is_dir():
        raise FileNotFoundError(f"Dataset not found: {source_root}")

    summary: dict[str, object] = {
        "source": str(source_root),
        "output": str(output_root),
        "classes": [name for _, name in RETAINED_CLASSES],
        "old_to_new_class_id": {
            str(old_id): new_id for old_id, new_id in class_remap.items()
        },
        "splits": {},
    }
    total_counts: Counter[int] = Counter()

    output_root.mkdir(parents=True, exist_ok=True)
    for split in ("train", "valid", "test"):
        source_images = source_root / split / "images"
        source_labels = source_root / split / "labels"
        output_images = output_root / split / "images"
        output_labels = output_root / split / "labels"
        output_images.mkdir(parents=True, exist_ok=True)
        output_labels.mkdir(parents=True, exist_ok=True)

        image_files = sorted(
            path
            for path in source_images.iterdir()
            if path.is_file() and path.suffix.lower() in IMAGE_SUFFIXES
        )
        split_counts: Counter[int] = Counter()
        link_modes: Counter[str] = Counter()
        included_images = 0
        excluded_unlabelled_images = 0
        empty_after_filter = 0
        malformed_lines = 0

        for source_image in image_files:
            source_label = source_labels / f"{source_image.stem}.txt"
            if not source_label.is_file():
                # Several of these images visibly contain PCB components. They are
                # excluded instead of being silently treated as background.
                excluded_unlabelled_images += 1
                continue

            destination_image = output_images / source_image.name
            destination_label = output_labels / source_label.name
            link_modes[hardlink_or_copy(source_image, destination_image)] += 1
            counts, malformed = convert_label(
                source_label, destination_label, class_remap
            )
            split_counts.update(counts)
            malformed_lines += malformed
            included_images += 1
            if not counts:
                empty_after_filter += 1

        total_counts.update(split_counts)
        summary["splits"][split] = {
            "source_images": len(image_files),
            "included_images": included_images,
            "excluded_images_without_label_file": excluded_unlabelled_images,
            "images_empty_after_class_filter": empty_after_filter,
            "malformed_label_lines": malformed_lines,
            "image_materialization": dict(sorted(link_modes.items())),
            "boxes_by_class": {
                RETAINED_CLASSES[class_id][1]: split_counts[class_id]
                for class_id in range(len(RETAINED_CLASSES))
            },
            "total_boxes": sum(split_counts.values()),
        }

    summary["total_boxes_by_class"] = {
        RETAINED_CLASSES[class_id][1]: total_counts[class_id]
        for class_id in range(len(RETAINED_CLASSES))
    }
    summary["total_boxes"] = sum(total_counts.values())

    write_dataset_yaml(output_root)
    summary_path = output_root / "dataset_summary.json"
    summary_path.write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    print(f"Wrote: {output_root / 'dataset.yaml'}")
    print(f"Wrote: {summary_path}")


if __name__ == "__main__":
    main()
