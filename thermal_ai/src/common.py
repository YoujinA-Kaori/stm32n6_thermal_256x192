#!/usr/bin/env python3
"""Shared helpers for the STM32N6 thermal object-detection workflow."""

from __future__ import annotations

import csv
import json
import math
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

import numpy as np


THERMAL_AI_ROOT = Path(__file__).resolve().parents[1]
WORKSPACE_ROOT = THERMAL_AI_ROOT.parent
DEFAULT_DATASET_CONFIG_PATH = THERMAL_AI_ROOT / "configs" / "dataset_config.json"
DEFAULT_TRAIN_CONFIG_PATH = THERMAL_AI_ROOT / "configs" / "train_config.json"


@dataclass(frozen=True)
class SampleRecord:
    """Description of one thermal frame sample."""

    split: str
    class_name: str
    label_index: int
    group_key: str
    bin_path: Path
    annotation_path: Path | None = None


@dataclass(frozen=True)
class AnnotatedObject:
    """One annotated bounding box inside a thermal frame."""

    class_name: str
    x_min: float
    y_min: float
    x_max: float
    y_max: float

    @property
    def center_x(self) -> float:
        """Return the horizontal box center in pixel coordinates."""
        return (self.x_min + self.x_max) * 0.5

    @property
    def center_y(self) -> float:
        """Return the vertical box center in pixel coordinates."""
        return (self.y_min + self.y_max) * 0.5

    @property
    def width(self) -> float:
        """Return the box width in pixels."""
        return max(self.x_max - self.x_min, 1.0)

    @property
    def height(self) -> float:
        """Return the box height in pixels."""
        return max(self.y_max - self.y_min, 1.0)

    @property
    def area(self) -> float:
        """Return the box area in pixels."""
        return self.width * self.height


@dataclass(frozen=True)
class SampleAnnotation:
    """Normalized multi-object annotation for one thermal frame."""

    primary_class_name: str | None
    objects: tuple[AnnotatedObject, ...]
    is_empty: bool


@dataclass(frozen=True)
class Detection:
    """One decoded detector prediction or ground-truth box."""

    class_name: str
    score: float
    x_min: float
    y_min: float
    x_max: float
    y_max: float

    @property
    def center_x(self) -> float:
        """Return the horizontal box center in pixel coordinates."""
        return (self.x_min + self.x_max) * 0.5

    @property
    def center_y(self) -> float:
        """Return the vertical box center in pixel coordinates."""
        return (self.y_min + self.y_max) * 0.5

    @property
    def width(self) -> float:
        """Return the box width in pixels."""
        return max(self.x_max - self.x_min, 0.0)

    @property
    def height(self) -> float:
        """Return the box height in pixels."""
        return max(self.y_max - self.y_min, 0.0)


def load_json(json_path: Path) -> dict[str, Any]:
    """Load a JSON file into a dictionary."""
    with json_path.open("r", encoding="utf-8") as fp:
        return json.load(fp)


def save_json(json_path: Path, payload: dict[str, Any] | list[Any]) -> None:
    """Save a JSON-compatible object using UTF-8 and stable indentation."""
    json_path.parent.mkdir(parents=True, exist_ok=True)
    with json_path.open("w", encoding="utf-8") as fp:
        json.dump(payload, fp, indent=2, ensure_ascii=False)
        fp.write("\n")


def load_dataset_config(config_path: Path | None = None) -> dict[str, Any]:
    """Load the dataset configuration file."""
    path = config_path if config_path is not None else DEFAULT_DATASET_CONFIG_PATH
    return load_json(path)


def load_train_config(config_path: Path | None = None) -> dict[str, Any]:
    """Load the training configuration file."""
    path = config_path if config_path is not None else DEFAULT_TRAIN_CONFIG_PATH
    return load_json(path)


def ensure_dir(dir_path: Path) -> Path:
    """Create a directory if it does not exist."""
    dir_path.mkdir(parents=True, exist_ok=True)
    return dir_path


def resolve_workspace_path(path_value: str | Path) -> Path:
    """Resolve a workspace-relative config path into an absolute path."""
    candidate = Path(path_value)
    if candidate.is_absolute():
        return candidate
    return WORKSPACE_ROOT / candidate


def annotation_path_for_bin(bin_path: Path) -> Path:
    """Return the sidecar JSON path for one thermal frame."""
    return bin_path.with_suffix(".json")


def load_sidecar_annotation(bin_path: Path) -> dict[str, Any] | None:
    """Load an optional per-frame JSON annotation sidecar."""
    annotation_path = annotation_path_for_bin(bin_path)
    if not annotation_path.exists():
        return None
    return load_json(annotation_path)


def _center_payload_to_box(object_payload: dict[str, Any]) -> tuple[float, float, float, float] | None:
    """Convert a legacy center-point payload into a small fallback box."""
    center_x = object_payload.get("center_x", object_payload.get("x"))
    center_y = object_payload.get("center_y", object_payload.get("y"))
    if center_x is None or center_y is None:
        return None

    box_width = float(object_payload.get("box_width_px", object_payload.get("box_size_px", 8.0)))
    box_height = float(object_payload.get("box_height_px", object_payload.get("box_size_px", 8.0)))
    half_width = max(box_width, 1.0) * 0.5
    half_height = max(box_height, 1.0) * 0.5
    return (
        float(center_x) - half_width,
        float(center_y) - half_height,
        float(center_x) + half_width,
        float(center_y) + half_height,
    )


def normalize_sidecar_annotation(annotation_payload: dict[str, Any] | None) -> SampleAnnotation | None:
    """Normalize a raw sidecar annotation payload into a typed bbox record."""
    if annotation_payload is None:
        return None

    primary_class_name = annotation_payload.get("primary_class_name", annotation_payload.get("class_name"))
    is_empty = bool(annotation_payload.get("empty", False)) or primary_class_name == "empty"
    objects_payload = annotation_payload.get("objects")
    objects: list[AnnotatedObject] = []

    if isinstance(objects_payload, list):
        for object_payload in objects_payload:
            if not isinstance(object_payload, dict):
                continue

            class_name = object_payload.get("class_name")
            if class_name is None or class_name == "empty":
                continue

            x_min = object_payload.get("x_min")
            y_min = object_payload.get("y_min")
            x_max = object_payload.get("x_max")
            y_max = object_payload.get("y_max")
            if None in (x_min, y_min, x_max, y_max):
                bbox_payload = object_payload.get("bbox")
                if isinstance(bbox_payload, list) and len(bbox_payload) == 4:
                    x_min, y_min, x_max, y_max = bbox_payload
                else:
                    legacy_box = _center_payload_to_box(object_payload)
                    if legacy_box is None:
                        continue
                    x_min, y_min, x_max, y_max = legacy_box

            x0 = float(x_min)
            y0 = float(y_min)
            x1 = float(x_max)
            y1 = float(y_max)
            objects.append(
                AnnotatedObject(
                    class_name=str(class_name),
                    x_min=min(x0, x1),
                    y_min=min(y0, y1),
                    x_max=max(x0, x1),
                    y_max=max(y0, y1),
                )
            )
    else:
        class_name = annotation_payload.get("class_name")
        if class_name is not None and class_name != "empty":
            legacy_box = _center_payload_to_box(annotation_payload)
            if legacy_box is not None:
                x_min, y_min, x_max, y_max = legacy_box
                objects.append(
                    AnnotatedObject(
                        class_name=str(class_name),
                        x_min=float(x_min),
                        y_min=float(y_min),
                        x_max=float(x_max),
                        y_max=float(y_max),
                    )
                )

    if not objects:
        return SampleAnnotation(
            primary_class_name=str(primary_class_name) if primary_class_name is not None else None,
            objects=tuple(),
            is_empty=True if is_empty or primary_class_name == "empty" else False,
        )

    return SampleAnnotation(
        primary_class_name=str(primary_class_name) if primary_class_name is not None else None,
        objects=tuple(objects),
        is_empty=False,
    )


def get_class_names(config: dict[str, Any]) -> list[str]:
    """Return the ordered class-name list."""
    return list(config["class_names"])


def get_detection_class_names(config: dict[str, Any]) -> list[str]:
    """Return the ordered class names that the detector predicts."""
    detector_cfg = config.get("detector", {})
    detection_class_names = detector_cfg.get("detection_class_names")
    if detection_class_names is None:
        detection_class_names = config.get("detection_class_names")
    if detection_class_names is None:
        return get_class_names(config)
    return list(detection_class_names)


def get_input_shape(config: dict[str, Any]) -> tuple[int, int, int]:
    """Return the TensorFlow NHWC input shape."""
    input_cfg = config["input"]
    return int(input_cfg["height"]), int(input_cfg["width"]), int(input_cfg["channels"])


def get_frame_shape(config: dict[str, Any]) -> tuple[int, int]:
    """Return the thermal frame height and width."""
    height, width, _ = get_input_shape(config)
    return height, width


def expected_frame_bytes(config: dict[str, Any]) -> int:
    """Return the expected payload size for one .bin temperature frame."""
    return int(config["raw_format"]["frame_bytes"])


def temp14_to_celsius(frame_u16: np.ndarray) -> np.ndarray:
    """Convert temp14 data from kelvin*16 to Celsius."""
    return frame_u16.astype(np.float32) / 16.0 - 273.15


def sanitize_temp_c_for_ai(temp_c: np.ndarray, config: dict[str, Any]) -> tuple[np.ndarray, dict[str, Any]]:
    """Replace physically implausible module temperature spikes for AI input only."""
    norm_cfg = config["normalization"]
    filter_cfg = norm_cfg.get("invalid_temperature_filter")
    if not isinstance(filter_cfg, dict) or not bool(filter_cfg.get("enabled", False)):
        return temp_c, {
            "invalid_filter_enabled": False,
            "invalid_pixel_count": 0,
            "invalid_pixel_ratio": 0.0,
            "invalid_replacement_c": None,
        }

    min_c = float(filter_cfg.get("min_c", -40.0))
    max_c = float(filter_cfg.get("max_c", 180.0))
    if max_c <= min_c:
        raise ValueError("normalization.invalid_temperature_filter.max_c must be greater than min_c")

    valid_mask = np.isfinite(temp_c) & (temp_c >= min_c) & (temp_c <= max_c)
    pixel_count = int(temp_c.size)
    valid_count = int(np.count_nonzero(valid_mask))
    invalid_count = pixel_count - valid_count
    if invalid_count <= 0:
        return temp_c, {
            "invalid_filter_enabled": True,
            "invalid_min_c": min_c,
            "invalid_max_c": max_c,
            "invalid_pixel_count": 0,
            "invalid_pixel_ratio": 0.0,
            "invalid_replacement_c": None,
        }

    replacement_mode = str(filter_cfg.get("replacement", "frame_percentile"))
    if valid_count > 0 and replacement_mode == "frame_percentile":
        replacement_percentile = float(filter_cfg.get("replacement_percentile", norm_cfg.get("background_percentile", 50.0)))
        replacement_c = float(np.percentile(temp_c[valid_mask], replacement_percentile))
    elif valid_count > 0:
        replacement_c = float(np.median(temp_c[valid_mask]))
    else:
        replacement_c = (min_c + max_c) * 0.5

    sanitized = temp_c.copy()
    sanitized[~valid_mask] = replacement_c
    return sanitized, {
        "invalid_filter_enabled": True,
        "invalid_min_c": min_c,
        "invalid_max_c": max_c,
        "invalid_pixel_count": invalid_count,
        "invalid_pixel_ratio": float(invalid_count / pixel_count) if pixel_count > 0 else 0.0,
        "invalid_replacement_c": replacement_c,
    }


def _normalize_temp_c_frame(temp_c: np.ndarray, config: dict[str, Any]) -> tuple[np.ndarray, dict[str, Any]]:
    """Normalize one Celsius thermal frame and return the normalization summary."""
    source_temp_c = temp_c
    temp_c, invalid_summary = sanitize_temp_c_for_ai(source_temp_c, config)
    norm_cfg = config["normalization"]
    mode = str(norm_cfg.get("mode", "celsius_linear_clip"))

    if mode == "celsius_linear_clip":
        temp_c_min = float(norm_cfg["temp_c_min"])
        temp_c_max = float(norm_cfg["temp_c_max"])
        scale = temp_c_max - temp_c_min
        if scale <= 0.0:
            raise ValueError("normalization.temp_c_max must be greater than normalization.temp_c_min")
        normalized = np.clip((temp_c - temp_c_min) / scale, 0.0, 1.0).astype(np.float32)
        summary = {
            "mode": mode,
            "source_min_c": float(np.min(source_temp_c)),
            "source_max_c": float(np.max(source_temp_c)),
            "source_mean_c": float(np.mean(source_temp_c)),
            "source_std_c": float(np.std(source_temp_c)),
            "sanitized_min_c": float(np.min(temp_c)),
            "sanitized_max_c": float(np.max(temp_c)),
            "normalization_min_c": temp_c_min,
            "normalization_max_c": temp_c_max,
            "normalized_min": float(np.min(normalized)),
            "normalized_max": float(np.max(normalized)),
        }
        summary.update(invalid_summary)
        return normalized, summary

    if mode == "delta_from_frame_percentile_clip":
        background_percentile = float(norm_cfg.get("background_percentile", 50.0))
        delta_c_min = float(norm_cfg["delta_c_min"])
        delta_c_max = float(norm_cfg["delta_c_max"])
        scale = delta_c_max - delta_c_min
        if scale <= 0.0:
            raise ValueError("normalization.delta_c_max must be greater than normalization.delta_c_min")

        background_temp_c = float(np.percentile(temp_c, background_percentile))
        delta_c = temp_c - background_temp_c
        normalized = np.clip((delta_c - delta_c_min) / scale, 0.0, 1.0).astype(np.float32)
        summary = {
            "mode": mode,
            "source_min_c": float(np.min(source_temp_c)),
            "source_max_c": float(np.max(source_temp_c)),
            "source_mean_c": float(np.mean(source_temp_c)),
            "source_std_c": float(np.std(source_temp_c)),
            "sanitized_min_c": float(np.min(temp_c)),
            "sanitized_max_c": float(np.max(temp_c)),
            "background_percentile": background_percentile,
            "background_temp_c": background_temp_c,
            "delta_min_c": float(np.min(delta_c)),
            "delta_max_c": float(np.max(delta_c)),
            "delta_mean_c": float(np.mean(delta_c)),
            "delta_std_c": float(np.std(delta_c)),
            "normalization_min_c": delta_c_min,
            "normalization_max_c": delta_c_max,
            "normalized_min": float(np.min(normalized)),
            "normalized_max": float(np.max(normalized)),
        }
        summary.update(invalid_summary)
        return normalized, summary

    raise ValueError(f"unsupported normalization mode: {mode}")


def _describe_temp_c_normalization(temp_c: np.ndarray, config: dict[str, Any]) -> dict[str, Any]:
    """Summarize how one Celsius thermal frame will be normalized."""
    _, summary = _normalize_temp_c_frame(temp_c, config)
    return summary


def describe_frame_normalization(frame_u16: np.ndarray, config: dict[str, Any]) -> dict[str, Any]:
    """Summarize how one raw frame will be normalized."""
    temp_c = temp14_to_celsius(frame_u16)
    return _describe_temp_c_normalization(temp_c, config)


def load_temp14_frame(bin_path: Path, config: dict[str, Any]) -> np.ndarray:
    """Load one raw .bin frame as a 2D uint16 array."""
    height, width = get_frame_shape(config)
    expected_bytes = expected_frame_bytes(config)
    actual_bytes = bin_path.stat().st_size
    if actual_bytes != expected_bytes:
        raise ValueError(
            f"{bin_path} has {actual_bytes} bytes, expected {expected_bytes} bytes "
            f"for {width}x{height} uint16 thermal data"
        )

    frame = np.fromfile(bin_path, dtype="<u2")
    return frame.reshape(height, width)


def normalize_temp14_frame(
    frame_u16: np.ndarray,
    config: dict[str, Any],
    add_channel_axis: bool = True,
) -> np.ndarray:
    """Convert raw temp14 pixels to the configured normalized float32 input tensor."""
    temp_c = temp14_to_celsius(frame_u16)
    normalized, _ = _normalize_temp_c_frame(temp_c, config)

    if add_channel_axis:
        normalized = normalized[..., np.newaxis]
    return normalized


def _stretch_preview_contrast(normalized_frame: np.ndarray) -> np.ndarray:
    """Apply display-only percentile stretch to improve grayscale preview readability."""
    preview_low = float(np.percentile(normalized_frame, 2.0))
    preview_high = float(np.percentile(normalized_frame, 98.0))
    if not np.isfinite(preview_low) or not np.isfinite(preview_high):
        return normalized_frame

    preview_span = preview_high - preview_low
    if preview_span < 1e-4:
        return normalized_frame

    return np.clip((normalized_frame - preview_low) / preview_span, 0.0, 1.0).astype(np.float32)


def temp14_preview_u8(frame_u16: np.ndarray, config: dict[str, Any]) -> np.ndarray:
    """Create an 8-bit grayscale preview using model normalization plus display-only contrast stretch."""
    normalized = normalize_temp14_frame(frame_u16, config, add_channel_axis=False)
    preview_normalized = _stretch_preview_contrast(normalized)
    return np.clip(np.round(preview_normalized * 255.0), 0, 255).astype(np.uint8)


def clip_box_to_frame(box: AnnotatedObject, frame_width: int, frame_height: int) -> AnnotatedObject:
    """Clamp a box to the valid frame bounds."""
    x_min = min(max(float(box.x_min), 0.0), float(frame_width - 1))
    y_min = min(max(float(box.y_min), 0.0), float(frame_height - 1))
    x_max = min(max(float(box.x_max), x_min + 1.0), float(frame_width))
    y_max = min(max(float(box.y_max), y_min + 1.0), float(frame_height))
    return AnnotatedObject(box.class_name, x_min, y_min, x_max, y_max)


def get_annotation_object_counts(annotation: SampleAnnotation | None, class_names: list[str]) -> dict[str, int]:
    """Count how many annotated objects belong to each class."""
    counts = {class_name: 0 for class_name in class_names}
    if annotation is None:
        return counts
    if annotation.is_empty or not annotation.objects:
        return counts
    for annotated_object in annotation.objects:
        if annotated_object.class_name in counts:
            counts[annotated_object.class_name] += 1
    return counts


def annotation_to_detections(annotation: SampleAnnotation | None, config: dict[str, Any]) -> list[Detection]:
    """Convert a normalized annotation into ground-truth detections."""
    if annotation is None or annotation.is_empty or not annotation.objects:
        return []

    frame_height, frame_width = get_frame_shape(config)
    detection_class_names = set(get_detection_class_names(config))
    detections: list[Detection] = []
    for annotated_object in annotation.objects:
        if annotated_object.class_name not in detection_class_names:
            continue
        clipped_box = clip_box_to_frame(annotated_object, frame_width, frame_height)
        detections.append(
            Detection(
                class_name=clipped_box.class_name,
                score=1.0,
                x_min=clipped_box.x_min,
                y_min=clipped_box.y_min,
                x_max=clipped_box.x_max,
                y_max=clipped_box.y_max,
            )
        )
    return detections


def get_detector_grid_shape(config: dict[str, Any]) -> tuple[int, int]:
    """Return the detector output grid height and width."""
    detector_cfg = config["detector"]
    return int(detector_cfg["grid_height"]), int(detector_cfg["grid_width"])


def get_detector_output_channels(config: dict[str, Any]) -> int:
    """Return the per-cell detector channel count."""
    return 1 + 4 + len(get_detection_class_names(config))


def get_detector_strides(config: dict[str, Any]) -> tuple[float, float]:
    """Return detector stride in input pixels for X and Y."""
    frame_height, frame_width = get_frame_shape(config)
    grid_height, grid_width = get_detector_grid_shape(config)
    return float(frame_width) / float(grid_width), float(frame_height) / float(grid_height)


def count_detection_target_collisions(annotation: SampleAnnotation | None, config: dict[str, Any]) -> int:
    """Count how many objects collide into already-occupied detector cells."""
    if annotation is None or annotation.is_empty or not annotation.objects:
        return 0

    detection_class_names = set(get_detection_class_names(config))
    grid_height, grid_width = get_detector_grid_shape(config)
    stride_x, stride_y = get_detector_strides(config)
    occupied_cells: set[tuple[int, int]] = set()
    collisions = 0

    for annotated_object in annotation.objects:
        if annotated_object.class_name not in detection_class_names:
            continue
        grid_x = min(grid_width - 1, max(0, int(annotated_object.center_x / stride_x)))
        grid_y = min(grid_height - 1, max(0, int(annotated_object.center_y / stride_y)))
        cell_key = (grid_x, grid_y)
        if cell_key in occupied_cells:
            collisions += 1
        else:
            occupied_cells.add(cell_key)

    return collisions


def build_detection_target(annotation: SampleAnnotation | None, config: dict[str, Any]) -> np.ndarray:
    """Encode one annotation into an anchor-free detector target tensor."""
    grid_height, grid_width = get_detector_grid_shape(config)
    frame_height, frame_width = get_frame_shape(config)
    detection_class_names = get_detection_class_names(config)
    class_to_index = {class_name: index for index, class_name in enumerate(detection_class_names)}
    stride_x, stride_y = get_detector_strides(config)
    target = np.zeros((grid_height, grid_width, get_detector_output_channels(config)), dtype=np.float32)

    if annotation is None or annotation.is_empty or not annotation.objects:
        return target

    eps = 1e-4
    for annotated_object in annotation.objects:
        class_index = class_to_index.get(annotated_object.class_name)
        if class_index is None:
            continue

        clipped_box = clip_box_to_frame(annotated_object, frame_width, frame_height)
        center_x = clipped_box.center_x
        center_y = clipped_box.center_y
        grid_x = min(grid_width - 1, max(0, int(center_x / stride_x)))
        grid_y = min(grid_height - 1, max(0, int(center_y / stride_y)))
        current_width = target[grid_y, grid_x, 3] * float(frame_width)
        current_height = target[grid_y, grid_x, 4] * float(frame_height)
        current_area = current_width * current_height

        if target[grid_y, grid_x, 0] > 0.0 and clipped_box.area <= current_area:
            continue

        target[grid_y, grid_x, :] = 0.0
        target[grid_y, grid_x, 0] = 1.0
        target[grid_y, grid_x, 1] = float(np.clip(center_x / stride_x - float(grid_x), eps, 1.0 - eps))
        target[grid_y, grid_x, 2] = float(np.clip(center_y / stride_y - float(grid_y), eps, 1.0 - eps))
        target[grid_y, grid_x, 3] = float(np.clip(clipped_box.width / float(frame_width), eps, 1.0 - eps))
        target[grid_y, grid_x, 4] = float(np.clip(clipped_box.height / float(frame_height), eps, 1.0 - eps))
        target[grid_y, grid_x, 5 + class_index] = 1.0

    return target


def sigmoid_array(values: np.ndarray) -> np.ndarray:
    """Apply a numerically stable sigmoid to a NumPy array."""
    clipped_values = np.clip(values, -60.0, 60.0)
    return 1.0 / (1.0 + np.exp(-clipped_values))


def detection_iou(box_a: Detection | AnnotatedObject, box_b: Detection | AnnotatedObject) -> float:
    """Compute IoU between two boxes."""
    inter_x_min = max(float(box_a.x_min), float(box_b.x_min))
    inter_y_min = max(float(box_a.y_min), float(box_b.y_min))
    inter_x_max = min(float(box_a.x_max), float(box_b.x_max))
    inter_y_max = min(float(box_a.y_max), float(box_b.y_max))
    inter_width = max(inter_x_max - inter_x_min, 0.0)
    inter_height = max(inter_y_max - inter_y_min, 0.0)
    inter_area = inter_width * inter_height
    if inter_area <= 0.0:
        return 0.0

    area_a = max(float(box_a.x_max) - float(box_a.x_min), 0.0) * max(float(box_a.y_max) - float(box_a.y_min), 0.0)
    area_b = max(float(box_b.x_max) - float(box_b.x_min), 0.0) * max(float(box_b.y_max) - float(box_b.y_min), 0.0)
    union_area = area_a + area_b - inter_area
    if union_area <= 0.0:
        return 0.0
    return float(inter_area / union_area)


def apply_detection_nms(detections: list[Detection], iou_threshold: float, max_detections: int) -> list[Detection]:
    """Run per-class non-maximum suppression on decoded detections."""
    kept: list[Detection] = []
    class_names = sorted({detection.class_name for detection in detections})
    for class_name in class_names:
        class_detections = sorted(
            [detection for detection in detections if detection.class_name == class_name],
            key=lambda detection: detection.score,
            reverse=True,
        )
        while class_detections and len(kept) < max_detections:
            best = class_detections.pop(0)
            kept.append(best)
            class_detections = [
                candidate
                for candidate in class_detections
                if detection_iou(best, candidate) < float(iou_threshold)
            ]
    kept.sort(key=lambda detection: detection.score, reverse=True)
    return kept[:max_detections]


def _passes_detection_post_filter(
    detection: Detection,
    frame_width: int,
    frame_height: int,
    filter_cfg: dict[str, Any],
) -> bool:
    """Apply lightweight geometric filters to decoded boxes."""
    min_width = float(filter_cfg.get("min_width_px", 0.0))
    min_height = float(filter_cfg.get("min_height_px", 0.0))
    if detection.width < min_width or detection.height < min_height:
        return False

    max_width = float(filter_cfg.get("max_width_px", frame_width))
    max_height = float(filter_cfg.get("max_height_px", frame_height))
    if detection.width > max_width or detection.height > max_height:
        return False

    box_area = detection.width * detection.height
    frame_area = float(frame_width * frame_height)
    min_area = float(filter_cfg.get("min_area_px", 0.0))
    max_area = float(filter_cfg.get("max_area_px", frame_area))
    min_area_ratio = float(filter_cfg.get("min_area_ratio", 0.0))
    max_area_ratio = float(filter_cfg.get("max_area_ratio", 1.0))
    if box_area < min_area or box_area > max_area:
        return False
    if frame_area > 0.0:
        area_ratio = box_area / frame_area
        if area_ratio < min_area_ratio or area_ratio > max_area_ratio:
            return False

    top_margin = float(filter_cfg.get("top_margin_px", 0.0))
    side_margin = float(filter_cfg.get("side_margin_px", 0.0))
    top_band_max_height = float(filter_cfg.get("top_band_max_height_px", 0.0))
    corner_max_width = float(filter_cfg.get("corner_max_width_px", 0.0))
    corner_max_height = float(filter_cfg.get("corner_max_height_px", 0.0))
    edge_strip_max_width = float(filter_cfg.get("edge_strip_max_width_px", 0.0))
    edge_strip_min_height = float(filter_cfg.get("edge_strip_min_height_px", 0.0))

    touches_top = detection.y_min <= top_margin
    touches_left = detection.x_min <= side_margin
    touches_right = detection.x_max >= float(frame_width) - side_margin

    if bool(filter_cfg.get("reject_boxes_touching_top_edge", False)) and touches_top:
        return False
    if bool(filter_cfg.get("reject_boxes_touching_side_edges", False)) and (touches_left or touches_right):
        return False
    if touches_top and detection.height <= top_band_max_height:
        return False
    if touches_top and touches_left and detection.width <= corner_max_width and detection.height <= corner_max_height:
        return False
    if touches_top and touches_right and detection.width <= corner_max_width and detection.height <= corner_max_height:
        return False
    if touches_left and detection.width <= edge_strip_max_width and detection.height >= edge_strip_min_height:
        return False
    if touches_right and detection.width <= edge_strip_max_width and detection.height >= edge_strip_min_height:
        return False

    return True


def apply_detection_post_filters(
    detections: list[Detection],
    config: dict[str, Any],
    frame_width: int,
    frame_height: int,
) -> list[Detection]:
    """Apply optional class-specific decoded-box filters before NMS."""
    detector_cfg = config.get("detector", {})
    post_cfg = detector_cfg.get("postprocess_filter")
    if not isinstance(post_cfg, dict) or not bool(post_cfg.get("enabled", False)):
        return detections

    filtered: list[Detection] = []
    for detection in detections:
        class_filter_cfg = post_cfg.get(detection.class_name)
        if not isinstance(class_filter_cfg, dict):
            filtered.append(detection)
            continue
        if _passes_detection_post_filter(detection, frame_width, frame_height, class_filter_cfg):
            filtered.append(detection)

    return filtered


def decode_detection_map(detection_map: np.ndarray, config: dict[str, Any]) -> list[Detection]:
    """Decode one detector output tensor into scored bounding boxes."""
    grid_height, grid_width = get_detector_grid_shape(config)
    frame_height, frame_width = get_frame_shape(config)
    detector_cfg = config["detector"]
    stride_x, stride_y = get_detector_strides(config)
    detection_class_names = get_detection_class_names(config)
    objectness_threshold = float(detector_cfg["objectness_threshold"])
    class_threshold = float(detector_cfg["class_threshold"])
    score_threshold = float(detector_cfg.get("score_threshold", class_threshold))
    class_thresholds = detector_cfg.get("class_thresholds", {})
    score_thresholds = detector_cfg.get("score_thresholds", {})
    nms_iou_threshold = float(detector_cfg["nms_iou_threshold"])
    max_detections = int(detector_cfg["max_detections_per_image"])

    if detection_map.shape[:2] != (grid_height, grid_width):
        raise ValueError(
            f"detection map shape mismatch: got {detection_map.shape[:2]}, expected {(grid_height, grid_width)}"
        )

    objectness_map = sigmoid_array(detection_map[..., 0])
    bbox_map = sigmoid_array(detection_map[..., 1:5])
    class_map = sigmoid_array(detection_map[..., 5 : 5 + len(detection_class_names)])

    detections: list[Detection] = []
    for grid_y in range(grid_height):
        for grid_x in range(grid_width):
            objectness_score = float(objectness_map[grid_y, grid_x])
            if objectness_score < objectness_threshold:
                continue

            class_scores = class_map[grid_y, grid_x]
            class_index = int(np.argmax(class_scores))
            class_score = float(class_scores[class_index])
            class_name = detection_class_names[class_index]
            detection_score = objectness_score * class_score
            effective_class_threshold = (
                float(class_thresholds.get(class_name, class_threshold))
                if isinstance(class_thresholds, dict)
                else class_threshold
            )
            effective_score_threshold = (
                float(score_thresholds.get(class_name, score_threshold))
                if isinstance(score_thresholds, dict)
                else score_threshold
            )
            if class_score < effective_class_threshold or detection_score < effective_score_threshold:
                continue

            offset_x, offset_y, width_norm, height_norm = bbox_map[grid_y, grid_x]
            center_x = (float(grid_x) + float(offset_x)) * stride_x
            center_y = (float(grid_y) + float(offset_y)) * stride_y
            box_width = max(float(width_norm) * float(frame_width), 1.0)
            box_height = max(float(height_norm) * float(frame_height), 1.0)
            x_min = max(center_x - box_width * 0.5, 0.0)
            y_min = max(center_y - box_height * 0.5, 0.0)
            x_max = min(center_x + box_width * 0.5, float(frame_width))
            y_max = min(center_y + box_height * 0.5, float(frame_height))

            detections.append(
                Detection(
                    class_name=class_name,
                    score=detection_score,
                    x_min=x_min,
                    y_min=y_min,
                    x_max=x_max,
                    y_max=y_max,
                )
            )

    detections = apply_detection_post_filters(detections, config, frame_width, frame_height)
    return apply_detection_nms(detections, nms_iou_threshold, max_detections)


def iter_bin_files(root_dir: Path) -> list[Path]:
    """Collect .bin files recursively in stable order."""
    return sorted(path for path in root_dir.rglob("*.bin") if path.is_file())


def strip_numeric_suffix(stem: str) -> str:
    """Strip a trailing numeric frame suffix from a filename stem."""
    stripped = re.sub(r"(?:[_-]frame)?[_-]?\d+$", "", stem, flags=re.IGNORECASE)
    stripped = stripped.rstrip("_-")
    return stripped or stem


def derive_group_key(bin_path: Path, class_root: Path) -> str:
    """Derive the session/group key for leakage-safe dataset splitting."""
    relative_path = bin_path.relative_to(class_root)
    if len(relative_path.parts) > 1:
        parent_key = "__".join(relative_path.parts[:-1]).replace(" ", "_")
        return parent_key

    return strip_numeric_suffix(relative_path.stem)


def scan_processed_split(
    processed_root: Path,
    split_name: str,
    class_names: list[str],
) -> list[SampleRecord]:
    """Scan one processed split into structured sample records."""
    samples: list[SampleRecord] = []
    split_root = processed_root / split_name

    for label_index, class_name in enumerate(class_names):
        class_root = split_root / class_name
        if not class_root.exists():
            continue

        for bin_path in iter_bin_files(class_root):
            relative_parent = bin_path.parent.relative_to(class_root)
            if str(relative_parent) == ".":
                group_key = strip_numeric_suffix(bin_path.stem)
            else:
                group_key = relative_parent.as_posix()

            sidecar_path = annotation_path_for_bin(bin_path)
            samples.append(
                SampleRecord(
                    split=split_name,
                    class_name=class_name,
                    label_index=label_index,
                    group_key=group_key,
                    bin_path=bin_path,
                    annotation_path=sidecar_path if sidecar_path.exists() else None,
                )
            )

    return samples


def write_csv(csv_path: Path, fieldnames: list[str], rows: Iterable[dict[str, Any]]) -> None:
    """Write a CSV file using the provided field order."""
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    with csv_path.open("w", encoding="utf-8", newline="") as fp:
        writer = csv.DictWriter(fp, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def to_python_scalar(value: Any) -> Any:
    """Convert NumPy scalar types into JSON-safe Python scalars."""
    if isinstance(value, np.generic):
        return value.item()
    return value


def make_json_safe(value: Any) -> Any:
    """Recursively convert NumPy-heavy data into JSON-safe Python objects."""
    if isinstance(value, dict):
        return {str(key): make_json_safe(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [make_json_safe(item) for item in value]
    if isinstance(value, np.ndarray):
        return value.tolist()
    if isinstance(value, Path):
        return str(value)
    if isinstance(value, (np.generic,)):
        return to_python_scalar(value)
    if isinstance(value, float):
        if math.isnan(value) or math.isinf(value):
            return None
    return value
