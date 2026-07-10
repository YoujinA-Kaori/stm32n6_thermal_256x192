#!/usr/bin/env python3
"""Metrics helpers for thermal object detection."""

from __future__ import annotations

from typing import Any

import numpy as np

from .common import Detection, detection_iou


def _compute_detection_tp_fp_fn(
    ground_truth_batches: list[list[Detection]],
    prediction_batches: list[list[Detection]],
    class_names: list[str],
    iou_threshold: float,
) -> tuple[dict[str, dict[str, Any]], list[float], list[float]]:
    """Match predictions to ground truth and accumulate per-class counts."""
    per_class = {
        class_name: {
            "support": 0,
            "predictions": 0,
            "tp": 0,
            "fp": 0,
            "fn": 0,
        }
        for class_name in class_names
    }
    matched_ious: list[float] = []
    matched_center_errors: list[float] = []

    for ground_truths, predictions in zip(ground_truth_batches, prediction_batches):
        predictions_by_class = {
            class_name: sorted(
                [prediction for prediction in predictions if prediction.class_name == class_name],
                key=lambda prediction: prediction.score,
                reverse=True,
            )
            for class_name in class_names
        }
        ground_truths_by_class = {
            class_name: [ground_truth for ground_truth in ground_truths if ground_truth.class_name == class_name]
            for class_name in class_names
        }

        for class_name in class_names:
            class_ground_truths = ground_truths_by_class[class_name]
            class_predictions = predictions_by_class[class_name]
            per_class[class_name]["support"] += len(class_ground_truths)
            per_class[class_name]["predictions"] += len(class_predictions)

            matched_prediction_indices: set[int] = set()
            for ground_truth in class_ground_truths:
                best_prediction_index = -1
                best_iou = 0.0
                for prediction_index, prediction in enumerate(class_predictions):
                    if prediction_index in matched_prediction_indices:
                        continue
                    overlap = detection_iou(ground_truth, prediction)
                    if overlap >= iou_threshold and overlap > best_iou:
                        best_iou = overlap
                        best_prediction_index = prediction_index

                if best_prediction_index >= 0:
                    matched_prediction = class_predictions[best_prediction_index]
                    matched_prediction_indices.add(best_prediction_index)
                    per_class[class_name]["tp"] += 1
                    matched_ious.append(best_iou)
                    matched_center_errors.append(
                        float(
                            np.sqrt(
                                (matched_prediction.center_x - ground_truth.center_x) ** 2
                                + (matched_prediction.center_y - ground_truth.center_y) ** 2
                            )
                        )
                    )
                else:
                    per_class[class_name]["fn"] += 1

            per_class[class_name]["fp"] += len(class_predictions) - len(matched_prediction_indices)

    return per_class, matched_ious, matched_center_errors


def compute_detection_metrics(
    ground_truth_batches: list[list[Detection]],
    prediction_batches: list[list[Detection]],
    class_names: list[str],
    iou_threshold: float,
) -> dict[str, Any]:
    """Compute dataset-level detection metrics at one IoU threshold."""
    if len(ground_truth_batches) != len(prediction_batches):
        raise ValueError("ground_truth_batches and prediction_batches must have the same image count")

    per_class_counts, matched_ious, matched_center_errors = _compute_detection_tp_fp_fn(
        ground_truth_batches,
        prediction_batches,
        class_names,
        iou_threshold,
    )

    per_class_metrics = []
    total_tp = 0
    total_fp = 0
    total_fn = 0
    exact_image_matches = 0

    for ground_truths, predictions in zip(ground_truth_batches, prediction_batches):
        ground_truth_classes = sorted(detection.class_name for detection in ground_truths)
        prediction_classes = sorted(detection.class_name for detection in predictions)
        if ground_truth_classes == prediction_classes:
            exact_image_matches += 1

    for class_name in class_names:
        class_counts = per_class_counts[class_name]
        tp = int(class_counts["tp"])
        fp = int(class_counts["fp"])
        fn = int(class_counts["fn"])
        support = int(class_counts["support"])
        predicted = int(class_counts["predictions"])
        precision = float(tp / (tp + fp)) if (tp + fp) > 0 else 0.0
        recall = float(tp / (tp + fn)) if (tp + fn) > 0 else 0.0
        f1_score = float(2.0 * precision * recall / (precision + recall)) if (precision + recall) > 0.0 else 0.0
        per_class_metrics.append(
            {
                "class_name": class_name,
                "precision": precision,
                "recall": recall,
                "f1_score": f1_score,
                "support": support,
                "predictions": predicted,
                "tp": tp,
                "fp": fp,
                "fn": fn,
            }
        )
        total_tp += tp
        total_fp += fp
        total_fn += fn

    micro_precision = float(total_tp / (total_tp + total_fp)) if (total_tp + total_fp) > 0 else 0.0
    micro_recall = float(total_tp / (total_tp + total_fn)) if (total_tp + total_fn) > 0 else 0.0
    micro_f1_score = (
        float(2.0 * micro_precision * micro_recall / (micro_precision + micro_recall))
        if (micro_precision + micro_recall) > 0.0
        else 0.0
    )

    macro_precision = float(np.mean([item["precision"] for item in per_class_metrics])) if per_class_metrics else 0.0
    macro_recall = float(np.mean([item["recall"] for item in per_class_metrics])) if per_class_metrics else 0.0
    macro_f1_score = float(np.mean([item["f1_score"] for item in per_class_metrics])) if per_class_metrics else 0.0
    exact_image_match_ratio = float(exact_image_matches / len(ground_truth_batches)) if ground_truth_batches else 0.0

    return {
        "iou_threshold": float(iou_threshold),
        "micro_precision": micro_precision,
        "micro_recall": micro_recall,
        "micro_f1_score": micro_f1_score,
        "macro_precision": macro_precision,
        "macro_recall": macro_recall,
        "macro_f1_score": macro_f1_score,
        "mean_iou": float(np.mean(matched_ious)) if matched_ious else 0.0,
        "mean_center_error_px": float(np.mean(matched_center_errors)) if matched_center_errors else None,
        "matched_boxes": len(matched_ious),
        "total_ground_truth_boxes": int(sum(len(items) for items in ground_truth_batches)),
        "total_predicted_boxes": int(sum(len(items) for items in prediction_batches)),
        "exact_image_match_ratio": exact_image_match_ratio,
        "per_class": per_class_metrics,
    }


def format_detection_report(class_names: list[str], metrics: dict[str, Any]) -> str:
    """Render a detection report into aligned plain text."""
    lines = []
    lines.append("class".ljust(28) + "precision  recall     f1-score   support   pred")
    for class_metric in metrics["per_class"]:
        lines.append(
            class_metric["class_name"].ljust(28)
            + f"{class_metric['precision']:9.4f}"
            + f"{class_metric['recall']:10.4f}"
            + f"{class_metric['f1_score']:11.4f}"
            + f"{class_metric['support']:10d}"
            + f"{class_metric['predictions']:7d}"
        )

    lines.append("")
    lines.append(f"IoU threshold{'':15}{metrics['iou_threshold']:.2f}")
    lines.append(f"micro avg{'':18}{metrics['micro_precision']:.4f}    {metrics['micro_recall']:.4f}    {metrics['micro_f1_score']:.4f}")
    lines.append(f"macro avg{'':18}{metrics['macro_precision']:.4f}    {metrics['macro_recall']:.4f}    {metrics['macro_f1_score']:.4f}")
    lines.append(f"mean IoU{'':20}{metrics['mean_iou']:.4f}")
    lines.append(f"exact image match{'':11}{metrics['exact_image_match_ratio']:.4f}")
    if metrics["mean_center_error_px"] is not None:
        lines.append(f"mean center error px{'':8}{metrics['mean_center_error_px']:.2f}")
    lines.append(f"matched boxes{'':15}{metrics['matched_boxes']}")
    lines.append(f"gt boxes{'':20}{metrics['total_ground_truth_boxes']}")
    lines.append(f"pred boxes{'':18}{metrics['total_predicted_boxes']}")
    return "\n".join(lines)


def build_collection_suggestions(class_names: list[str], metrics: dict[str, Any]) -> list[str]:
    """Generate data-collection suggestions from per-class detection metrics."""
    suggestions: list[str] = []
    for class_metric in metrics["per_class"]:
        class_name = class_metric["class_name"]
        precision = float(class_metric["precision"])
        recall = float(class_metric["recall"])
        support = int(class_metric["support"])
        if support <= 0:
            continue

        if recall < 0.65:
            if class_name == "circuit_board_normal":
                suggestions.append("circuit_board_normal recall is low; add more normal-board boxes across different board sizes, loads, and ambient temperatures.")
            elif class_name == "circuit_board_abnormal_hotspot":
                suggestions.append("circuit_board_abnormal_hotspot recall is low; add more abnormal heating boxes under varied board layouts and thermal backgrounds.")
            else:
                suggestions.append(f"{class_name} recall is low; collect more session-diverse detection boxes for this class.")

        if precision < 0.65:
            if class_name == "circuit_board_abnormal_hotspot":
                suggestions.append("circuit_board_abnormal_hotspot precision is low; add clearer class boundaries and tighter boxes for local hot regions.")
            elif class_name == "circuit_board_normal":
                suggestions.append("circuit_board_normal precision is low; add more full-board normal samples and avoid mixing normal-board labels with isolated hotspot boxes.")
            else:
                suggestions.append(f"{class_name} precision is low; tighten box labels and add hard negatives for this class.")

    if not suggestions and metrics["micro_f1_score"] < 0.85:
        suggestions.append("overall detection F1 is still limited; increase box diversity across sessions, distances, and target sizes.")
    return suggestions
