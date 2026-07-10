#!/usr/bin/env python3
"""Helpers for TFLite export, inspection, and inference validation."""

from __future__ import annotations

from pathlib import Path
from typing import Any

import numpy as np

try:
    import tensorflow as tf
except ImportError as exc:  # pragma: no cover
    tf = None
    _tf_import_error = exc
else:
    _tf_import_error = None


def require_tensorflow():
    """Return TensorFlow or stop with a clear missing-dependency message."""
    if tf is None:  # pragma: no cover
        raise SystemExit(
            "TensorFlow is required for TFLite export and validation. "
            f"Install it in the active Python environment first: {_tf_import_error}"
        )
    return tf


def load_tflite_interpreter(model_path: Path):
    """Load a TFLite interpreter and allocate tensors."""
    tf_module = require_tensorflow()
    interpreter = tf_module.lite.Interpreter(model_path=str(model_path))
    interpreter.allocate_tensors()
    return interpreter


def tensor_detail_dict(detail: dict[str, Any]) -> dict[str, Any]:
    """Extract shape, dtype, and quantization info for one TFLite tensor."""
    scale, zero_point = detail.get("quantization", (0.0, 0))
    return {
        "name": detail.get("name", ""),
        "shape": [int(value) for value in detail["shape"]],
        "dtype": np.dtype(detail["dtype"]).name,
        "scale": float(scale),
        "zero_point": int(zero_point),
    }


def quantize_input_tensor(input_tensor: np.ndarray, tensor_detail: dict[str, Any]) -> np.ndarray:
    """Convert float32 input data into the TFLite input tensor dtype."""
    dtype = np.dtype(tensor_detail["dtype"])
    if dtype == np.float32:
        return input_tensor.astype(np.float32)

    scale, zero_point = tensor_detail.get("quantization", (0.0, 0))
    if scale == 0.0:
        raise ValueError("quantized input tensor has zero scale")

    qmin = np.iinfo(dtype).min
    qmax = np.iinfo(dtype).max
    quantized = np.round(input_tensor / scale + zero_point)
    quantized = np.clip(quantized, qmin, qmax)
    return quantized.astype(dtype)


def dequantize_output_tensor(output_tensor: np.ndarray, tensor_detail: dict[str, Any]) -> np.ndarray:
    """Convert a TFLite output tensor back to float32 probabilities."""
    dtype = np.dtype(tensor_detail["dtype"])
    if dtype == np.float32:
        return output_tensor.astype(np.float32)

    scale, zero_point = tensor_detail.get("quantization", (0.0, 0))
    if scale == 0.0:
        raise ValueError("quantized output tensor has zero scale")

    return (output_tensor.astype(np.float32) - float(zero_point)) * float(scale)


def run_tflite_inference(interpreter, frame_tensor: np.ndarray) -> list[np.ndarray]:
    """Run one normalized frame through a TFLite interpreter."""
    input_detail = interpreter.get_input_details()[0]
    output_details = interpreter.get_output_details()

    batched_input = frame_tensor[np.newaxis, ...] if frame_tensor.ndim == 3 else frame_tensor
    encoded_input = quantize_input_tensor(batched_input, input_detail)
    interpreter.set_tensor(input_detail["index"], encoded_input)
    interpreter.invoke()
    decoded_outputs: list[np.ndarray] = []
    for output_detail in output_details:
        raw_output = interpreter.get_tensor(output_detail["index"])
        decoded_outputs.append(dequantize_output_tensor(raw_output, output_detail))
    return decoded_outputs


def inspect_tflite_model(model_path: Path) -> dict[str, Any]:
    """Inspect a TFLite model and return input/output metadata."""
    interpreter = load_tflite_interpreter(model_path)
    input_details = interpreter.get_input_details()
    output_details = interpreter.get_output_details()
    return {
        "model_path": str(model_path),
        "inputs": [tensor_detail_dict(detail) for detail in input_details],
        "outputs": [tensor_detail_dict(detail) for detail in output_details],
    }
