#!/usr/bin/env python3
"""Split raw thermal .bin files into train/val/test."""

from __future__ import annotations

import argparse
import random
import shutil
from collections import defaultdict
from pathlib import Path
from typing import Any

import _bootstrap

_bootstrap.setup_python_path()

from src.common import (
    annotation_path_for_bin,
    derive_group_key,
    ensure_dir,
    get_class_names,
    load_dataset_config,
    make_json_safe,
    resolve_workspace_path,
    save_json,
    write_csv,
)


def resolve_split_group_key(bin_path: Path, class_root: Path, group_policy: str) -> str:
    """Resolve the split grouping key for one raw sample."""
    if group_policy == "per_file":
        relative_path = bin_path.relative_to(class_root)
        return str(relative_path.with_suffix("")).replace("\\", "__").replace("/", "__").replace(" ", "_")

    if group_policy == "session_or_filename_prefix":
        return derive_group_key(bin_path, class_root)

    raise SystemExit(
        f"unsupported split.group_policy: {group_policy} "
        "(expected 'session_or_filename_prefix' or 'per_file')"
    )


def build_group_map(
    raw_root: Path,
    class_names: list[str],
    group_policy: str,
) -> dict[str, dict[str, list[Path]]]:
    """Collect raw files into class -> group -> file list."""
    grouped_files: dict[str, dict[str, list[Path]]] = {}

    for class_name in class_names:
        class_root = raw_root / class_name
        class_groups: dict[str, list[Path]] = defaultdict(list)
        if class_root.exists():
            for bin_path in sorted(path for path in class_root.rglob("*.bin") if path.is_file()):
                group_key = resolve_split_group_key(bin_path, class_root, group_policy)
                class_groups[group_key].append(bin_path)
        grouped_files[class_name] = dict(class_groups)

    return grouped_files


def assign_groups_to_splits(
    class_groups: dict[str, list[Path]],
    split_ratios: dict[str, float],
    rng: random.Random,
) -> dict[str, str]:
    """Assign whole groups to splits while approximating target counts."""
    group_items = list(class_groups.items())
    weighted_items = [
        (len(files), rng.random(), group_key, files)
        for group_key, files in group_items
    ]
    weighted_items.sort(key=lambda item: (-item[0], item[1]))

    total_files = sum(len(files) for _, files in class_groups.items())
    targets = {name: total_files * ratio for name, ratio in split_ratios.items()}
    current_counts = {name: 0 for name in split_ratios}
    assignment: dict[str, str] = {}

    for _, _, group_key, files in weighted_items:
        split_name = max(
            split_ratios.keys(),
            key=lambda name: (targets[name] - current_counts[name], -current_counts[name]),
        )
        assignment[group_key] = split_name
        current_counts[split_name] += len(files)

    non_zero_splits = [name for name, ratio in split_ratios.items() if ratio > 0.0]
    groups_per_split = {name: [group for group, split_name in assignment.items() if split_name == name] for name in split_ratios}

    if len(weighted_items) >= len(non_zero_splits):
        for split_name in non_zero_splits:
            if groups_per_split[split_name]:
                continue

            donor_split = max(groups_per_split, key=lambda name: len(groups_per_split[name]))
            if len(groups_per_split[donor_split]) <= 1:
                continue

            donor_group = min(groups_per_split[donor_split], key=lambda group_key: len(class_groups[group_key]))
            groups_per_split[donor_split].remove(donor_group)
            groups_per_split[split_name].append(donor_group)
            assignment[donor_group] = split_name

    return assignment


def copy_split_files(
    raw_root: Path,
    processed_root: Path,
    class_names: list[str],
    grouped_files: dict[str, dict[str, list[Path]]],
    split_assignment: dict[str, dict[str, str]],
) -> list[dict[str, Any]]:
    """Copy grouped files into processed split folders and return manifest rows."""
    manifest_rows: list[dict[str, Any]] = []

    for split_name in ("train", "val", "test"):
        for class_name in class_names:
            ensure_dir(processed_root / split_name / class_name)

    for label_index, class_name in enumerate(class_names):
        class_groups = grouped_files[class_name]
        for group_key, files in class_groups.items():
            split_name = split_assignment[class_name][group_key]
            for source_path in files:
                target_dir = ensure_dir(processed_root / split_name / class_name / group_key)
                target_path = target_dir / source_path.name
                shutil.copy2(source_path, target_path)
                annotation_source_path = annotation_path_for_bin(source_path)
                annotation_target_path = target_path.with_suffix(".json")
                annotation_copied = False
                if annotation_source_path.exists():
                    shutil.copy2(annotation_source_path, annotation_target_path)
                    annotation_copied = True
                manifest_rows.append(
                    {
                        "split": split_name,
                        "class_name": class_name,
                        "label_index": label_index,
                        "group_key": group_key,
                        "source_path": str(source_path.relative_to(raw_root)),
                        "processed_path": str(target_path.relative_to(processed_root)),
                        "annotation_path": str(annotation_target_path.relative_to(processed_root)) if annotation_copied else "",
                        "annotation_copied": int(annotation_copied),
                        "bytes": int(target_path.stat().st_size),
                    }
                )

    return manifest_rows


def summarize_manifest(
    class_names: list[str],
    grouped_files: dict[str, dict[str, list[Path]]],
    split_assignment: dict[str, dict[str, str]],
) -> dict[str, Any]:
    """Build a JSON-safe summary for the generated processed dataset."""
    summary: dict[str, Any] = {"classes": {}, "splits": {}}

    for class_name in class_names:
        class_groups = grouped_files[class_name]
        summary["classes"][class_name] = {
            "groups": len(class_groups),
            "files": sum(len(files) for files in class_groups.values()),
            "annotation_files": 0,
            "split_counts": {split_name: 0 for split_name in ("train", "val", "test")},
        }

        for group_key, files in class_groups.items():
            split_name = split_assignment[class_name][group_key]
            summary["classes"][class_name]["split_counts"][split_name] += len(files)
            summary["classes"][class_name]["annotation_files"] += sum(
                1 for source_path in files if annotation_path_for_bin(source_path).exists()
            )

    split_totals = {split_name: 0 for split_name in ("train", "val", "test")}
    for class_name in class_names:
        for split_name, count in summary["classes"][class_name]["split_counts"].items():
            split_totals[split_name] += int(count)

    summary["splits"] = split_totals
    return make_json_safe(summary)


def build_arg_parser() -> argparse.ArgumentParser:
    """Build the CLI argument parser."""
    parser = argparse.ArgumentParser(description="Split thermal raw .bin files into train/val/test")
    parser.add_argument("--config", type=Path, default=None, help="Path to dataset_config.json")
    parser.add_argument("--raw-root", type=Path, default=None, help="Raw dataset root, default from config")
    parser.add_argument("--processed-root", type=Path, default=None, help="Processed dataset root, default from config")
    parser.add_argument("--seed", type=int, default=None, help="Override split seed")
    parser.add_argument("--overwrite", action="store_true", help="Delete existing processed output before writing")
    return parser


def main() -> int:
    """Program entry point."""
    args = build_arg_parser().parse_args()
    config = load_dataset_config(args.config)
    class_names = get_class_names(config)

    raw_root = args.raw_root if args.raw_root is not None else resolve_workspace_path(config["dataset_paths"]["raw_root"])
    processed_root = (
        args.processed_root
        if args.processed_root is not None
        else resolve_workspace_path(config["dataset_paths"]["processed_root"])
    )

    split_cfg = config["split"]
    seed = args.seed if args.seed is not None else int(split_cfg["seed"])
    group_policy = str(split_cfg.get("group_policy", "session_or_filename_prefix"))
    split_ratios = {
        "train": float(split_cfg["train_ratio"]),
        "val": float(split_cfg["val_ratio"]),
        "test": float(split_cfg["test_ratio"]),
    }

    ratio_sum = split_ratios["train"] + split_ratios["val"] + split_ratios["test"]
    if abs(ratio_sum - 1.0) > 1e-6:
        raise SystemExit(f"split ratios must sum to 1.0, got {ratio_sum}")

    if not raw_root.exists():
        raise SystemExit(f"raw dataset root does not exist: {raw_root}")

    if processed_root.exists():
        if not args.overwrite:
            raise SystemExit(f"processed root already exists, re-run with --overwrite: {processed_root}")
        shutil.rmtree(processed_root)

    rng = random.Random(seed)
    grouped_files = build_group_map(raw_root, class_names, group_policy)
    split_assignment: dict[str, dict[str, str]] = {}

    for class_name in class_names:
        class_groups = grouped_files[class_name]
        if not class_groups:
            print(f"warning: class {class_name} has no .bin files under {raw_root / class_name}")
            split_assignment[class_name] = {}
            continue

        if group_policy != "per_file" and len(class_groups) < 3:
            print(
                f"warning: class {class_name} only has {len(class_groups)} session/group buckets; "
                "consider adding more capture sessions before trusting validation metrics"
            )

        split_assignment[class_name] = assign_groups_to_splits(class_groups, split_ratios, rng)

    manifest_rows = copy_split_files(raw_root, processed_root, class_names, grouped_files, split_assignment)
    ensure_dir(processed_root)

    write_csv(
        processed_root / "split_manifest.csv",
        ["split", "class_name", "label_index", "group_key", "source_path", "processed_path", "annotation_path", "annotation_copied", "bytes"],
        manifest_rows,
    )

    summary = summarize_manifest(class_names, grouped_files, split_assignment)
    save_json(processed_root / "split_summary.json", summary)

    for split_name in ("train", "val", "test"):
        split_count = sum(1 for row in manifest_rows if row["split"] == split_name)
        print(f"{split_name}: {split_count} file(s)")

    print(f"processed dataset written to {processed_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
