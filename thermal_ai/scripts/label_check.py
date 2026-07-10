#!/usr/bin/env python3
"""Check whether dataset labels match the expected config-defined layout."""

from __future__ import annotations

import argparse
from pathlib import Path

import _bootstrap

_bootstrap.setup_python_path()

from src.common import get_class_names, load_dataset_config


def check_raw_labels(root_dir: Path, expected_labels: list[str]) -> int:
    """Validate raw dataset class folders."""
    actual_labels = sorted(path.name for path in root_dir.iterdir() if path.is_dir())
    expected_set = set(expected_labels)
    actual_set = set(actual_labels)

    missing = sorted(expected_set - actual_set)
    unexpected = sorted(actual_set - expected_set)

    print(f"raw labels: {actual_labels}")
    if missing:
        print(f"missing labels: {missing}")
    if unexpected:
        print(f"unexpected labels: {unexpected}")

    for class_name in expected_labels:
        class_root = root_dir / class_name
        if class_root.exists():
            count = len(list(class_root.rglob('*.bin')))
            annotation_count = len(list(class_root.rglob('*.json')))
            print(f"  {class_name}: {count} .bin file(s), {annotation_count} annotation file(s)")

    return 1 if missing or unexpected else 0


def check_processed_labels(root_dir: Path, expected_labels: list[str]) -> int:
    """Validate processed dataset split/class folders."""
    error_count = 0
    for split_name in ("train", "val", "test"):
        split_root = root_dir / split_name
        actual_labels = sorted(path.name for path in split_root.iterdir() if path.is_dir()) if split_root.exists() else []
        expected_set = set(expected_labels)
        actual_set = set(actual_labels)

        missing = sorted(expected_set - actual_set)
        unexpected = sorted(actual_set - expected_set)
        print(f"[{split_name}] labels: {actual_labels}")
        if missing:
            print(f"  missing labels: {missing}")
            error_count += len(missing)
        if unexpected:
            print(f"  unexpected labels: {unexpected}")
            error_count += len(unexpected)

        for class_name in expected_labels:
            class_root = split_root / class_name
            if class_root.exists():
                count = len(list(class_root.rglob('*.bin')))
                annotation_count = len(list(class_root.rglob('*.json')))
                print(f"  {class_name}: {count} .bin file(s), {annotation_count} annotation file(s)")

    return 1 if error_count > 0 else 0


def build_arg_parser() -> argparse.ArgumentParser:
    """Build the CLI argument parser."""
    parser = argparse.ArgumentParser(description="Check raw or processed dataset labels")
    parser.add_argument("--config", type=Path, default=None, help="Path to dataset_config.json")
    parser.add_argument("--root", type=Path, required=True, help="Dataset root to inspect")
    parser.add_argument("--stage", choices=("raw", "processed"), required=True, help="Dataset stage")
    return parser


def main() -> int:
    """Program entry point."""
    args = build_arg_parser().parse_args()
    config = load_dataset_config(args.config)
    expected_labels = get_class_names(config)

    if not args.root.exists():
        raise SystemExit(f"dataset root does not exist: {args.root}")

    if args.stage == "raw":
        return check_raw_labels(args.root, expected_labels)

    return check_processed_labels(args.root, expected_labels)


if __name__ == "__main__":
    raise SystemExit(main())
